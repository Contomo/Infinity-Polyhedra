"""serial_manager.py - handles USB/serial I/O, geo-dump routing and central logging
-------------------------------------------------------------------------------
Responsibilities
* reconnect loop with back-off
* send() helper that logs every outbound command (tagged [sent])
* drain() that:
    - logs every inbound line (tagged [recv])
    - optional hide/filter for #noprefix# sections or regex masks
    - automatically issues a #dumpgeo# once after (re)connect when no geometry
* viewer bridge for live geometry (#geo# … #endgeo#) via debug_viewer.py
* public helper toggle_hidden() to switch visibility of filtered traffic
"""
import sys, time, subprocess, tempfile, os, re, logging
from pathlib import Path

import serial, serial.tools.list_ports
from colorama import init as clr_init, Fore, Style

import config

clr_init(autoreset=True)

# ── logging setup (file + stdout) ─────────────────────────────────────────
LOG_DIR = Path("logs")
LOG_DIR.mkdir(exist_ok=True)
log_path = LOG_DIR / time.strftime("serial_%Y%m%d_%H%M%S.txt")
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(message)s",
    handlers=[
        logging.FileHandler(log_path, encoding="utf-8"),
        logging.StreamHandler(sys.stdout),
    ],
)

# ── globals ───────────────────────────────────────────────────────────────
ser               = None          #   serial.Serial instance
retry_interval    = config.FAST_RETRY
last_reconnect    = 0.0
recv_buffer       = b""           #   unparsed bytes stash

viewer_proc       = None          #   debug_viewer.py process
viewer_in         = None          #   its stdin

collecting        = False         #   inside #geo# … #endgeo#
buffer_lines      = []
active_name       = "viewer"

got_geometry      = False         #   at least one geo dump seen?
pending_face      = None          #   face idx requested before geometry

map_dump_mode     = False         #   inside #noprefix# … #endnoprefix#
show_hidden       = False         #   runtime toggle for filtered traffic

connect_time      = 0
sent_dump_request = False
# pattern to decide whether to hide a line when show_hidden is False
HIDE_RE = re.compile(r"^#.*#$")   #  lines enclosed in #...#  (incl. noprefix zones)

# ── helpers ───────────────────────────────────────────────────────────────

def _log_recv(text: str):
    prefix = "[recv] "
    if _should_hide(text):
        if not show_hidden:
            return  # silently drop
        prefix = "[hidden] "
    logging.info(prefix + text)


def _log_sent(cmd: str):
    logging.info("[sent] " + cmd)


def _should_hide(text: str) -> bool:
    """Return True if line belongs to a hidden category."""
    return map_dump_mode or bool(HIDE_RE.match(text))


# ── public API ────────────────────────────────────────────────────────────

def toggle_hidden():
    global show_hidden
    show_hidden = not show_hidden
    logging.info("[info] toggled hidden display → %s", show_hidden)


# ── serial core ───────────────────────────────────────────────────────────

def list_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


def close_serial():
    global ser
    if ser:
        try:
            ser.close()
        except Exception:
            pass
    ser = None


def try_reconnect():
    """Attempt (re)connection every `retry_interval` seconds."""
    global ser, last_reconnect, retry_interval, got_geometry, connect_time, sent_dump_request
    if ser and ser.is_open:
        if not sent_dump_request and time.time() >= connect_time + 250:
            sent_dump_request = True
            send(config.COMMANDS['dump'])
        return True

    now = time.time()
    if now - last_reconnect < retry_interval:
        return False
    last_reconnect = now

    close_serial()
    for port in list_ports():
        try:
            logging.info("Opening %s …", port)
            ser = serial.Serial(port, config.BAUD, timeout=0.1, write_timeout=0.1)
            retry_interval = config.FAST_RETRY
            logging.info(Fore.GREEN + f"Connected on {port}")
            got_geometry = False          # fresh session - no geo yet
            # Immediately ask the MCU for geometry
            connect_time = time.time()
            
            return True
        except Exception as e:
            logging.warning("  %s: %s", port, e)
    retry_interval = config.SLOW_RETRY
    return False


def send(cmd: str):
    """Send a command with NL, log + colourise."""
    _log_sent(cmd)

    if not ser or not ser.is_open:
        return
    try:
        ser.write((cmd + "\n").encode())
    except Exception as e:
        logging.error("[send error] %s", e)
        close_serial()


# ── geometry viewer bridge ────────────────────────────────────────────────

def _viewer_send(packet):
    """Ensure debug_viewer.py is running, stream packet(s) to stdin."""
    global viewer_proc, viewer_in
    if viewer_proc is None or viewer_proc.poll() is not None:
        try:
            viewer_proc = subprocess.Popen(
                [sys.executable, "debug_viewer.py"],
                text=True, stdin=subprocess.PIPE, close_fds=True,
            )
            viewer_in = viewer_proc.stdin
            logging.info(Fore.YELLOW + "[viewer] debug window started")
        except Exception as e:
            logging.error("[viewer error] %s", e)
            viewer_proc = viewer_in = None
            return

    try:
        if isinstance(packet, str):
            viewer_in.write(packet + "\n")
        else:
            viewer_in.writelines(l + "\n" for l in packet)
        viewer_in.flush()
    except Exception as e:
        logging.error("[viewer pipe] %s", e)


# ── drain() - parse inbound stream ────────────────────────────────────────

def drain():
    """Consume bytes, split into CR/LF-terminated lines, handle meta-tags."""
    global recv_buffer, collecting, buffer_lines, got_geometry, pending_face, map_dump_mode

    if not ser or not ser.is_open:
        return
    try:
        data = ser.read(ser.in_waiting or 1)
        if not data:
            return
        recv_buffer += data
        while True:
            sep_idx = min((i for i in (recv_buffer.find(b"\n"), recv_buffer.find(b"\r")) if i != -1), default=-1)
            if sep_idx == -1:
                break
            line, recv_buffer = recv_buffer[:sep_idx], recv_buffer[sep_idx+1:]
            text = line.decode(errors="replace").rstrip("\r")
            if not text:
                continue

            # Handle noprefix sections (raw passthrough, hidden by default)
            if text == "#noprefix#":
                map_dump_mode = True
                _log_recv(text)
                continue
            if text == "#endnoprefix#" and map_dump_mode:
                _log_recv(text)
                map_dump_mode = False
                continue

            if map_dump_mode:
                _log_recv(text)
                continue

            # Geometry stream start
            if text.startswith("#geo#"):
                collecting = True
                buffer_lines = []
                buffer_lines = [text]
                _log_recv(text)
                continue

                        # End of geometry
            if text.startswith("#endgeo#") and collecting:
                buffer_lines.append(text)
                collecting = False
                got_geometry = True
                _viewer_send(buffer_lines)
                _log_recv(text)
                continue

            # Face selection
            if text.startswith("#face#"):
                # Merke dir den gewünschten Face-Index, sende ihn aber erst
                # nachdem der Viewer den neuen Geo-Dump verarbeitet hat.
                pending_face = int(text.split(None, 1)[1])
                _log_recv(text)
                continue


            # Collect geometry lines
            if collecting:
                buffer_lines.append(text)
                #_log_recv(text)  # still log for completeness
                continue

            # Normal line - log & print
            _log_recv(text)
    except Exception as e:
        logging.error("[rx error] %s", e)
        close_serial()
