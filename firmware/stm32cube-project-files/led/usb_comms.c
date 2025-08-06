/* --------------------------------------------------------------------------
 * usb_comms.c – buffered printf / command handler for STM32‑USB‑CDC
 *
 *  • Captures *all* early‑boot printf output in a RAM ring.
 *  • Flushes it when USB is CONFIGURED (dev_state) – no DTR gating.
 *  • Continues via the TX‑complete ISR and opportunistic flush in _write().
 *
 *   Revised: allow flush immediately once CONFIGURED to avoid missed window.
 * -------------------------------------------------------------------------- */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "led_debug.h"
#include "usb_comms.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "stm32f4xx_hal.h"   // for HAL_GetTick()

static char     rx_buffer[256];
static uint8_t  rx_len;
static bool     rx_ready = false;

static char     tx_buffer[TX_BUF_SIZE];
static uint32_t tx_head = 0;   /* next byte to send */
static uint32_t tx_tail = 0;   /* next free slot   */

extern USBD_HandleTypeDef hUsbDeviceFS;
volatile bool host_open = false;
static uint32_t host_open_tick = 0;

/* -------------------------------------------------------------------------- */
/* DTR handshake – record state and attempt flush if already configured       */
/* -------------------------------------------------------------------------- */
void usb_set_host_open(bool s)
{
	if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED || HAL_GetTick() <= 1000) return;
    host_open = s;
    host_open_tick = HAL_GetTick();
    //USBD_UsrLog("DTR is now %s at tick %lu", s ? "ON" : "OFF", host_open_tick);
}


/* -------------------------------------------------------------------------- */
/* RX path                                                                     */
/* -------------------------------------------------------------------------- */
uint8_t usb_comms_receive(uint8_t *Buf, uint32_t Len)
{
	if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && rx_len >= 10 && host_open != true){
		host_open = true;
		host_open_tick = 0;
	}
    rx_len = (Len < sizeof(rx_buffer)) ? Len : sizeof(rx_buffer) - 1;
    memcpy(rx_buffer, Buf, rx_len);
    rx_buffer[rx_len] = '\0';
    rx_ready = true;
    return USBD_OK;
}

/* -------------------------------------------------------------------------- */
/* Ring‑buffer helpers                                                         */
/* -------------------------------------------------------------------------- */
static uint32_t room_left(void)
{
    return (tx_head <= tx_tail)
         ? (TX_BUF_SIZE - (tx_tail - tx_head) - 1)
         : (tx_head - tx_tail - 1);
}

/* -------------------------------------------------------------------------- */
/* printf backend – enqueue chars and opportunistic flush when configured      */
/* -------------------------------------------------------------------------- */

int _write(int file, char *ptr, int len)
{
    int to_return = len;            // <-- hier merken wir uns den Original-Len
    (void)file;

    while (len--) {
        if (!room_left()) {
            tx_head = (tx_head + TX_DROP_CHUNK) % TX_BUF_SIZE;
        }
        tx_buffer[tx_tail] = *ptr++;
        tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
    }

    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && host_open) {
        flush_usb_buffer();
    }

    return to_return;               // <-- statt 0
}


/* -------------------------------------------------------------------------- */
/* TX path – send chunks, advance head                                         */
/* -------------------------------------------------------------------------- */

static uint8_t flush_now(void)
{
    if (tx_head == tx_tail) return USBD_OK;

    uint32_t chunk = (tx_tail > tx_head)
                   ? (tx_tail - tx_head)
                   : (TX_BUF_SIZE - tx_head);

    extern uint8_t UserTxBufferFS[];   //from usbd_cdc_if.c
    if (chunk > APP_TX_DATA_SIZE) chunk = APP_TX_DATA_SIZE;

    memcpy(UserTxBufferFS, &tx_buffer[tx_head], chunk);
    uint8_t res = CDC_Transmit_FS(UserTxBufferFS, chunk);
    if (res == USBD_OK) {
        tx_head = (tx_head + chunk) % TX_BUF_SIZE;
    }
    return res;
}

/* -------------------------------------------------------------------------- */
/* Drain buffer – only require CONFIGURED, ignore DTR gating                  */
/* -------------------------------------------------------------------------- */

void flush_usb_buffer(void)
{
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED || !host_open) return;
    if ((HAL_GetTick() - host_open_tick) < 250) return;
    // drain until empty or USB busy
    while (tx_head != tx_tail) {
        if (flush_now() != USBD_OK) break;
    }
}

/* -------------------------------------------------------------------------- */
/* Called from CDC Tx complete callback to continue draining                   */
/* -------------------------------------------------------------------------- */

void usb_tx_complete_isr(void)
{
    flush_usb_buffer();
}

/* ────────────────────────────────────────────────────────────────────────  */
/* --------------------------------------------------------------------------
 * DEBUG MODE SWITCHING
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * usb_comms_process() – unified parser for single-letter debug commands
 * --------------------------------------------------------------------------
 *   f  – face index    (dbg_face)
 *   b  – bar index     (dbg_bar_index)
 *   e  – edge/slot     (dbg_edge_slot)
 *   m  – debug mode    (cycles or relative delta)
 *   r  – reverse / flip current logical edge
 *   save  – persist current mapping & dump tables
 *   help  – list valid commands
 *
 *   Suffix syntax:
 *       <nothing>   -> +1     (same as "++")
 *       "++"        -> +1
 *       "--"        -> -1
 *       <float>     -> delta value (e.g. "0.1", "1.5", "-3")
 *
 *   Examples:
 *       "f"     , "f++"   , "f  2" , "f-1"
 *       "b--"   , "e  0.5", "m--", "r" , "save"
 * -------------------------------------------------------------------------- */

static void send_help(void)
{/* no actually, please someone help me */
	USBD_UsrLog("Valid cmds:\n f b e m [++|--|<float>]\n r (flip)\n save\n help\n");
}

/* ────────────────────────────────────────────────────────────────────────  */
static float parse_delta(const char *arg)
{
    if (*arg == '\0')                          return  1.0f;      /* implicit ++ */
    if (strcmp(arg, "++") == 0)               return  1.0f;
    if (strcmp(arg, "--") == 0)               return -1.0f;
    return (float)atof(arg);   /* handles "+1", "-2.5", "0" … */
}
/* ────────────────────────────────────────────────────────────────────────  */

/* # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP # TEMP   */

#include "polyhedron.h"
#include "geo_debug.h"

void printPolys(void) {
    Polyhedron *poly = poly_alloc();
    if (!poly) {
        USBD_UsrLog("Error: out of heap\n");
        return;
    }

    //poly_init_icosahedron(poly);
    //geo_dump_wireframe(poly, "icosa");

    poly_init_dodecahedron(poly);
    geo_dump_wireframe(poly, "dodeca");

    //poly_init_cube(poly);
    //geo_dump_wireframe(poly, "cube");

    //poly_init_octahedron(poly);
    //geo_dump_wireframe(poly, "octa");

    //poly_init_icosidodecahedron(poly);
    //geo_dump_wireframe(poly, "icosidodecahedron");

    //poly_init_rhombitruncated_icosidodecahedron(poly);
    //geo_dump_wireframe(poly, "rhombitruncated_icosidodecahedron");

    poly_free(poly);
}


static uint8_t mode = 0;

/* # END TEMP # END TEMP # END TEMP # END TEMP # END TEMP # END TEMP # END TEMP   */
extern Polyhedron poly;
#define GEO_DUMP_CMD   "#dumpgeo#"
/* ────────────────────────────────────────────────────────────────────────  */
static bool usb_greeted = false; // only say hewooo once
void usb_comms_process(void)
{
    if (!usb_greeted &&
        hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        usb_greeted = true;
        USBD_UsrLog("Debug interface ready. Type \"help\" for commands.\n");
    }

    if (!rx_ready) return;
    rx_ready = false;

    /* 1. Trim whitespace + CR/LF --------------------------------------- */
    char *msg = rx_buffer;
    while (isspace((unsigned char)*msg)) ++msg;
    size_t len = strlen(msg);
    while (len && isspace((unsigned char)msg[len-1])) msg[--len] = '\0';
    if (!len) return;

    /* 2. Full-word commands -------------------------------------------- */
    if (strcmp(msg, "save") == 0) {
        debug_save_and_dump();
        return;
    }
    if (strcmp(msg, "help") == 0) {
        send_help();
        return;
    }
    if (strcmp(msg, GEO_DUMP_CMD) == 0) {
           geo_dump_model(&poly, "poly");
           return;
       }

    /* 3. Single-letter commands ---------------------------------------- */
    char cmd = msg[0];
    const char *arg = msg + 1;
    while (isspace((unsigned char)*arg)) ++arg;   /* tolerate spaces */

    switch (cmd) {
    case 'f':  /* face */
        debug_change_face(parse_delta(arg));
        break;

    case 'b':  /* bar / physical bar index */
        debug_change_bar(parse_delta(arg));
        break;

    case 'e':  /* edge slot in current face */
        debug_change_slot(parse_delta(arg));
        break;

    case 'm': {
        int delta = (int)parse_delta(arg);
        mode = (mode + delta) % 7;  // wrap around if needed
        debug_change_mode((uint8_t)mode);
        USBD_UsrLog("Mode: %d", mode);
        break;
    }

    case 'h':
    	debug_change_hue(parse_delta(arg));
    	break;

    case 'r':  /* reverse / flip */
        if (*arg != '\0') { send_help(); return; }
        debug_toggle_flip();
        break;

    case 'g':  /* geo → dump vertices & edges */
        if (*arg != '\0') { send_help(); return; }
        printPolys();
        break;

    default:
        send_help();
        return;
    }

    debug_ui_tick();
}
