#ifndef USB_COMMS_H
#define USB_COMMS_H

#include <stdint.h>
#include <stdbool.h>
#include "usbd_cdc_if.h"     // for CDC_Receive_FS, CDC_Control_FS, CDC_TransmitCplt_FS
#include "usb_device.h"      // for MX_USB_DEVICE_Init, USBD_HandleTypeDef
#include "config.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifndef DEBUG_TX_BUF_SIZE
#define TX_BUF_SIZE   4096    /* ~200 average log lines */
#else
#define TX_BUF_SIZE DEBUG_TX_BUF_SIZE
#endif

#ifndef DEBUG_TX_DROP_CHUNK
#define TX_DROP_CHUNK 256     /* drop oldest bytes on overflow */
#else
#define TX_BUF_SIZE DEBUG_TX_DROP_CHUNK
#endif

/* --------------------------------------------------------------------------
 * USB CDC COMMAND INTERFACE
 * -------------------------------------------------------------------------- */
extern volatile bool host_open;

/**
 * @brief  Called by CDC layer when data is received.
 * @param  Buf Pointer to incoming byte buffer
 * @param  Len Length of data received
 * @return USBD_OK
 */
uint8_t usb_comms_receive(uint8_t* Buf, uint32_t Len);

/**
 * @brief  Called by CDC_Control hook when host toggles DTR (Data Terminal Ready).
 * @param  open True if host asserted DTR (port open), false if closed.
 */
void usb_set_host_open(bool open);

/**
 * @brief  Called from CDC transmit-complete callback to continue flushing.
 */
void usb_tx_complete_isr(void);

/**
 * @brief  Poll-based handler to process any received commands.
 *         Should be called regularly in main loop.
 *         Sends greeting message once USB is connected.
 */
void usb_comms_process(void);

/**
 * @brief  Retargeted printf write handler to buffer USB logs.
 *         Called by newlib (e.g. printf).
 * @param  file File descriptor (unused)
 * @param  ptr Pointer to data to write
 * @param  len Number of bytes to write
 * @return Number of bytes written
 */
int _write(int file, char *ptr, int len);

/**
 * @brief  Flushes the USB log buffer (e.g., after printing many lines).
 */
void flush_usb_buffer(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_COMMS_H */
