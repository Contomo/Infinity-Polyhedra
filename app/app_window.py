
import sys
import logging
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QSplitter, QMessageBox,
    QVBoxLayout, QLabel, QLineEdit, QPlainTextEdit, QPushButton, QHBoxLayout
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QColor, QPalette
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

import config

from controller_core import ControllerCore
import serial_manager
import numpy as np
from debug_viewer import Viewer, _parse


class QtConsoleHandler(logging.Handler):
    def __init__(self, console_widget):
        super().__init__()
        self.console = console_widget
        self.setFormatter(logging.Formatter('%(message)s'))

    def emit(self, record):
        msg = self.format(record)
        self.console.appendPlainText(msg)


class AppWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Poly Debug")
        self.resize(1200, 800)

        # Set gray theme
        theme_color = QColor(53, 53, 53)
        theme_recessed_color = QColor(23, 23, 23)
        text_color = QColor(220, 220, 220)

        pal = self.palette()
        pal.setColor(QPalette.Window, theme_color)
        pal.setColor(QPalette.Base, theme_color)
        pal.setColor(QPalette.Text, text_color)
        self.setPalette(pal)

        # Core for serial + input
        logging.info("[info] Starting ControllerCore...")
        self.core = ControllerCore()

        # Build UI
        main_splitter = QSplitter(Qt.Horizontal)
        main_splitter.setStretchFactor(0, 1)
        main_splitter.setStretchFactor(1, 2)

        # Left vertical splitter: controls, sent, received  
        left_splitter = QSplitter(Qt.Vertical)  
        left_splitter.setChildrenCollapsible(False)
        left_splitter.setHandleWidth(4)


        # ─── Autoscroll controls at top ─────────────────────  
        ctrl_widget = QWidget()  
        ctrl_widget.setPalette(pal)  
        hbox = QHBoxLayout(ctrl_widget)  
        hbox.setContentsMargins(10,8,5,5); hbox.setSpacing(5)  
        lbl_auto = QLabel("Autoscroll:")  
        lbl_auto.setStyleSheet("color: #ddd;")  
        hbox.addWidget(lbl_auto)  

        self.autoscroll_sent = True  
        btn_sent = QPushButton("TX"); btn_sent.setCheckable(True); btn_sent.setChecked(True); btn_sent.setFixedSize(40,20)
        btn_sent.setStyleSheet('QPushButton {background-color: #1a1a1a; color: #eee;}')
        btn_sent.clicked.connect(lambda v: setattr(self, 'autoscroll_sent', v))  
        hbox.addWidget(btn_sent)  

        self.autoscroll_recv = True  
        btn_recv = QPushButton("RX"); btn_recv.setCheckable(True); btn_recv.setChecked(True); btn_recv.setFixedSize(40,20)  
        btn_recv.setStyleSheet('QPushButton {background-color: #1a1a1a; color: #eee;}')
        btn_recv.clicked.connect(lambda v: setattr(self, 'autoscroll_recv', v))  
        hbox.addWidget(btn_recv)  
        hbox.addStretch(1)  
        

        # ─── Gyro‐stream toggle ─────────────────────────────
        self.btn_gyro = QPushButton("Gyro Off")
        self.btn_gyro.setCheckable(True)
        self.btn_gyro.setFixedSize(60, 20)
        hbox.addWidget(self.btn_gyro)

        # QTimer to fire at config.GYRO_SEND_RATE Hz
        self.gyro_timer = QTimer(self)
        self.gyro_timer.setInterval(int(1000 / config.GYRO_SEND_RATE))
        self.gyro_timer.timeout.connect(self._send_gyro)
        self.btn_gyro.toggled.connect(self.on_gyro_toggled)




        ctrl_widget.setFixedHeight(30)
        left_splitter.addWidget(ctrl_widget) 

        # ───────────────────────────────────────────────────  
        # 1) Sent & log console – define it first
        self.sent_console = QPlainTextEdit()
        self.sent_console.setReadOnly(True)
        self.sent_console.setStyleSheet("background-color: #1a1a1a; color: #eee;")

        # 2) Composite: Sent console + Input line
        comp_widget = QWidget()
        comp_widget.setPalette(pal)
        comp_layout = QVBoxLayout(comp_widget)
        comp_layout.setContentsMargins(5, 0, 0, 5)
        comp_layout.setSpacing(2)

        # Sent console
        comp_layout.addWidget(self.sent_console)

        # Input line (fixed height)
        self.input_line = QLineEdit()
        self.input_line.setPlaceholderText("Type help for help...")
        self.input_line.setFixedHeight(24)
        self.input_line.setStyleSheet("background-color: #1a1a1a; color: #eee;")
        self.input_line.returnPressed.connect(self._on_enter)
        # add input line to composite layout
        comp_layout.addWidget(self.input_line)

        
        # wrap the composite widget and add to splitter
        left_splitter.addWidget(comp_widget)

        # 3) Received console (2/3)
        self.recv_console = QPlainTextEdit()
        self.recv_console.setReadOnly(True)
        self.recv_console.setStyleSheet("background-color: #1a1a1a; color: #eee;")
        # wrap in a widget for consistent margins/border
        recv_widget = QWidget()
        recv_widget.setPalette(pal)
        recv_layout = QVBoxLayout(recv_widget)
        recv_layout.setContentsMargins(5, 5, 5, 5)
        recv_layout.setSpacing(2)
        recv_layout.addWidget(self.recv_console)
        left_splitter.addWidget(recv_widget)


        # Plot area
        canvas_widget = QWidget()
        canvas_widget.setPalette(pal)
        canvas_layout = QVBoxLayout(canvas_widget)
        self.figure = Figure(facecolor='#353535')
        self.canvas = FigureCanvas(self.figure)
        self.canvas.toolbar_visible = False
        canvas_layout.addWidget(self.canvas)

        main_splitter.addWidget(left_splitter)
        main_splitter.addWidget(canvas_widget)
        self.setCentralWidget(main_splitter)




        # ─── Metrics display (rolling averages) ─────────────────────────────────
        # how many samples to average over
        self.metrics_history = config.METRICS_HISTORY
        self.frame_times = []
        self.anim_times = []

        # build a little bar above the autoscroll controls
        metrics_widget = QWidget()
        metrics_layout = QHBoxLayout(metrics_widget)
        metrics_layout.setContentsMargins(10, 5, 5, 0)
        metrics_layout.setSpacing(15)

        self.lbl_frame_time = QLabel("Avg frame: – ms")
        self.lbl_frame_time.setStyleSheet("color: #ddd;")
        metrics_layout.addWidget(self.lbl_frame_time)

        self.lbl_anim_time = QLabel("Avg anim: – ms")
        self.lbl_anim_time.setStyleSheet("color: #ddd;")
        metrics_layout.addWidget(self.lbl_anim_time)

        metrics_layout.addStretch(1)
        metrics_widget.setFixedHeight(25)

        # insert above the existing ctrl_widget (which is at index 0)
        left_splitter.insertWidget(0, metrics_widget)
# ───────────────────────────────────────────────────────────────────────

        # Logging handler to sent_console
        handler = QtConsoleHandler(self.sent_console)
        logging.getLogger().addHandler(handler)

        # override serial_manager._log_recv to filter out metrics & #face# tags
        orig_log_recv = serial_manager._log_recv
        def _filtered_log_recv(msg):
            # ─── intercept frametime updates ───────────────────────────
            if msg.startswith("#frametime"):
                import re
                m = re.match(r"#frametime\s+(\d+)#", msg)
                if m:
                    us  = int(m.group(1))       # raw microseconds
                    val = us / 1000.0           # convert to milliseconds
                    self.frame_times.append(val)
                    if len(self.frame_times) > self.metrics_history:
                        self.frame_times.pop(0)
                    avg = sum(self.frame_times) / len(self.frame_times)
                    self.lbl_frame_time.setText(f"Avg frame: {avg:.2f} ms")
                return

            if msg.startswith("#animtime"):
                import re
                m = re.match(r"#animtime\s+(\d+)#", msg)
                if m:
                    us  = int(m.group(1))
                    val = us / 1000.0          # convert to milliseconds
                    self.anim_times.append(val)
                    if len(self.anim_times) > self.metrics_history:
                        self.anim_times.pop(0)
                    avg = sum(self.anim_times) / len(self.anim_times)
                    self.lbl_anim_time.setText(f"Avg anim: {avg:.2f} ms")
                return

            # ─── drop face tags as before ───────────────────────────────
            if msg.startswith("#face#"):
                return

            # ─── normal RX logging ──────────────────────────────────────
            # note: we bypass orig_log_recv so that
            # only messages we want actually appear in the console
            self.recv_console.appendPlainText(msg)
            if self.autoscroll_recv:
                sb = self.recv_console.verticalScrollBar()
                sb.setValue(sb.maximum())

        serial_manager._log_recv = _filtered_log_recv



        # Connect input
        self.input_line.returnPressed.connect(self._on_enter)


        # Timers
        self.step_timer = QTimer(self)
        self.step_timer.timeout.connect(self._on_timer)
        self.step_timer.start(5)

        self.viewer_timer = QTimer(self)
        self.viewer_timer.timeout.connect(self._update_viewer)
        self.viewer_timer.start(200)

        # Viewer placeholder
        self.viewer = None


    def on_gyro_toggled(self, checked: bool):
        """Start or stop sending gyro data to the MCU."""
        self.btn_gyro.setText("Gyro On" if checked else "Gyro Off")
        if checked:
            self.gyro_timer.start()
        else:
            self.gyro_timer.stop()


    def _send_gyro(self):
        """
        Called on each timer tick.  Grabs the latest gyro tuple
        from ControllerCore.get_gyro(), if available, and sends it.
        """
        try:
            gyro_data = self.core.get_gyro()
        except AttributeError:
            # Core doesn’t implement get_gyro yet
            self.on_gyro_toggled(False)
            QMessageBox.warning(
                self, "Gyro Error",
                "ControllerCore.get_gyro() not implemented."
            )
            return

        if gyro_data is None:
            QMessageBox.warning(self, "Warning", "No Gyro data available.")
            self.on_gyro_toggled(False)
            return

        x, y, z = gyro_data
        cmd = f"#gyro x={x:+.3f},y={y:+.3f},z={z:+.3f}#"
        try:
            serial_manager.send(cmd)
        except Exception as e:
            # turn off on error
            self.on_gyro_toggled(False)
            QMessageBox.warning(
                self, "Serial Error",
                f"Failed to send gyro data:\n{e}"
            )

            

    def _on_enter(self):
        cmd = self.input_line.text().strip()
        if cmd:
            serial_manager.send(cmd)
            self.input_line.clear()

    def _update_viewer(self):
        if getattr(serial_manager, 'got_geometry', False): # is true if false
            lines = serial_manager.buffer_lines
            serial_manager.buffer_lines = []
            try:
                V, H, E, F = _parse(lines)
            except Exception as exc:
                logging.error(f"Parsing failed: {exc}")
                serial_manager.got_geometry = False
                if self.viewer:
                    self.figure.clf()
                    self.viewer = None
                return

            if V.size == 0 or len(E) == 0:
                serial_manager.got_geometry = False
                return

            if self.viewer is None:
                self.viewer = Viewer(V, H, E, F, figure=self.canvas.figure)
            else:
                self.viewer.update_geometry(lines)

            serial_manager.got_geometry = False

        if serial_manager.pending_face is not None and self.viewer:
            new_idx = serial_manager.pending_face
            serial_manager.pending_face = None
            if new_idx != self.viewer.current_face:
                self.viewer.show_face(new_idx)

    def _on_timer(self):
        self.core.step()



    def closeEvent(self, event):
        self.step_timer.stop()
        self.viewer_timer.stop()
        self.core.shutdown()
        super().closeEvent(event)

if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(message)s')
    app = QApplication(sys.argv)
    win = AppWindow()
    win.show()
    sys.exit(app.exec_())

