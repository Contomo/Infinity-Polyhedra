/*
 * led_render.h – Header for dynamic framebuffer + SPI strip driver
 *
 * Provides initialization, rendering control, and color conversion APIs.
 */

#ifndef _LED_RENDER_H_
#define _LED_RENDER_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "config.h"              /* application-level overrides */

#ifdef __cplusplus
extern "C" {
#endif

/* total RAM we’re allowed to use for all frames (more so a sanity check to error if above) */
#ifndef LED_RENDER_MAX_ALLOC
  #define LED_RENDER_MAX_ALLOC  (16 * 1024)
#endif

/* what order the strip expects bytes in (usually “GRB” or “RGB”) */
#ifndef LED_COLOR_ORDER
  #define LED_COLOR_ORDER       "GRB"
#endif

/**
 * 8-bit RGB color structure
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_8b;

/**
 * Framebuffer holding RGB data for each logical pixel
 */
extern rgb_8b *framebuffer;

/**
 * Indicates whether the renderer is initialized and ready
 */
extern bool render_ready;

/**
 * Global brightness factor (0-255)
 */
extern uint8_t g_global_brightness;

/**
 * Initialize the LED renderer
 * @param total_pixels   Total number of logical LEDs
 * @param strip_count    Number of SPI-connected strips
 * @param spi_handles    Array of SPI handle pointers (length = strip_count)
 * @return true on success, false on failure or insufficient memory
 */
bool init_render(uint16_t total_pixels,
                 uint8_t strip_count,
                 SPI_HandleTypeDef * const *spi_handles);

/**
 * Shutdown the LED renderer and free resources
 */
void led_render_shutdown(void);

/**
 * Set all pixels in framebuffer to the same RGB color
 */
void set_all_pixels_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set a single pixel to a specific RGB color
 * @param idx  Pixel index (0-based)
 */
void set_pixel_color(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * Add pixel color with clamping (saturating addition)
 * @param idx  Pixel index (0-based)
 */
void add_pixel_color(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * Convert HSV (8-bit) to RGB (8-bit)
 */
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * HSV->RGB conversion with rainbow mapping
 */
void hsv_to_rgb_rainbow(uint8_t hue, uint8_t sat, uint8_t val,
                     uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * Hue difference calculation (for smooth transitions)
 * @param a  Start hue
 * @param b  End hue
 * @return signed shortest distance from a to b
 */
int16_t hue_diff(uint8_t a, uint8_t b);

/**
 * Generate a random hue
 * @return random value 0-255
 */
uint8_t random_hue(void);

/**
 * Push current framebuffer out to LED strips via SPI
 */
void update_leds(void);

#ifdef __cplusplus
}
#endif

#endif /* _LED_RENDER_H_ */
