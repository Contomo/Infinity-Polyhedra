/* --------------------------------------------------------------------------
 * led_render.c – dynamic framebuffer + SPI strip driver (no topology logic)
 * -------------------------------------------------------------------------- */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "led_render.h"
#include "stm32f4xx_hal.h"

#include "config.h"

#if defined(LED_DEBUG_RENDER) || defined(LED_DEBUG_RENDER_HEAP)
#include "usb_comms.h"   /* USBD_UsrLog() */
#endif




/* --------------------------------------------------------------------------
 * PRIVATE STATE
 * -------------------------------------------------------------------------- */

static uint16_t  pixels_total   = 0;   /* logical LEDs */
static uint16_t  pixels_per_str = 0;   /* ceil(total/strip_cnt) */
static uint8_t   strip_cnt      = 0;   /* saved from init */
static SPI_HandleTypeDef **spi_arr = NULL; /* external array copy */

rgb_8b  *framebuffer  = NULL;
static uint8_t *strip_buffer = NULL;   /* strip_cnt * pixels_per_str * 9 */

bool    render_ready        = false;
uint8_t g_global_brightness = 255;

static uint32_t encode_tbl[256];
static uint8_t color_map[3];



#ifdef GAMMA_CORRECTION
/* ─────────────────────────────────────────────────────────────────────────
 * Set all pixels to the same RGB value
 *
 */
static uint8_t gamma8[256];
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Set all pixels to the same RGB value
 *
 */
static size_t bytes_free_heap(void);
static void   free_buffers(void);
static void   init_encode_tbl(void);
static void   init_color_map(void);

#ifdef GAMMA_CORRECTION
/**
 * Build the gamma correction table (must be called before first frame)
 * @param gamma  Gamma exponent to use
 */
static void buildGammaTable(float gamma);
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Set all pixels to the same RGB value
 *
 */
bool init_render(uint16_t total_pixels,
                 uint8_t   strip_count,
                 SPI_HandleTypeDef * const *spi_handles)
{
    render_ready = false;

    if (!total_pixels || !strip_count || !spi_handles)
        return false;

    pixels_total   = total_pixels;
    strip_cnt      = strip_count;
    pixels_per_str = (pixels_total + strip_cnt - 1) / strip_cnt; // ceil div

    const size_t fb_bytes = sizeof(rgb_8b) * pixels_total;
    // for each strip: pixels_per_str LEDs × 9 bytes + 1 latch byte
    const size_t sb_bytes = (size_t)strip_cnt * (pixels_per_str * 9 + 1);

    const size_t alloc_total = fb_bytes + sb_bytes;

    if (LED_RENDER_MAX_ALLOC && alloc_total > LED_RENDER_MAX_ALLOC)
        return false;

    free_buffers();

    framebuffer  = malloc(fb_bytes);
    strip_buffer = malloc(sb_bytes);
    spi_arr      = (SPI_HandleTypeDef **)spi_handles;

    if (!framebuffer || !strip_buffer) {
        free_buffers();
        return false;
    }

    memset(framebuffer,  0, fb_bytes);
    memset(strip_buffer, 0, sb_bytes);
    init_encode_tbl();
    init_color_map();
#ifdef GAMMA_CORRECTION
    buildGammaTable(GAMMA_CORRECTION);
#endif

#ifdef LED_DEBUG_RENDER
    USBD_UsrLog(
        "───[ Led-Render-Heap ]───\n"
        "=========================\n"
        "   %-5u pixels\n"
        "   %-5u strips\n"
        "   %-5.1f kB framebuffer\n"
        "   %-5.1f kB stripbuffer(s)\n"
        "   %-5.1f kB total\n"
        "   %-5.1f kB heap left\n"
    	"\n ",
        (unsigned)pixels_total,
        (unsigned)strip_cnt,
        fb_bytes    / 1024.0f,
        sb_bytes    / 1024.0f,
        alloc_total / 1024.0f,
        bytes_free_heap() / 1024.0f
    );
#endif
	render_ready = true;
	return true;
}


void led_render_shutdown(void)
{
    free_buffers();
    render_ready = false;
}



#ifdef GAMMA_CORRECTION
static void buildGammaTable(float gamma) {
  for (int i = 0; i < 256; ++i) {
    gamma8[i] = (uint8_t)(powf(i / 255.0f, gamma) * 255.0f + 0.5f);
  }
}
#endif


/* ─────────────────────────────────────────────────────────────────────────
 * Set all pixels to the same RGB value
 *
 */
static inline uint8_t qadd8(uint8_t a, uint8_t b) {
    uint8_t s = a + b;
    // if wraparound happened, s < a → clamp
    return (s < a) ? 255 : s;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Set all pixels to the same RGB value
 *
 */
void set_all_pixels_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!render_ready) return;
    for(uint16_t i = 0; i < pixels_total; ++i) {
        framebuffer[i] = (rgb_8b){r, g, b};
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Set individual pixel to a specific RGB value
 *
 */
void set_pixel_color(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (!render_ready || idx >= pixels_total) return;
    framebuffer[idx] = (rgb_8b){r, g, b};
}

/* ─────────────────────────────────────────────────────────────────────────
 * Add pixel color with clamping (saturating addition)
 *
 */
void add_pixel_color(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if ((r | g | b) == 0) return;
    rgb_8b *c = &framebuffer[idx];
    c->r = qadd8(c->r, r);
    c->g = qadd8(c->g, g);
    c->b = qadd8(c->b, b);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Add pixel color with clamping (saturating addition)
 *
 */
void subtract_pixel_color(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
    rgb_8b c = framebuffer[idx];
    c.r = (c.r > r) ? c.r - r : 0;
    c.g = (c.g > g) ? c.g - g : 0;
    c.b = (c.b > b) ? c.b - b : 0;
    framebuffer[idx] = c;
}


/* ─────────────────────────────────────────────────────────────────────────
 * HSV → RGB conversion (8-bit fixed-point, fast integer math)
 *
 */
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    uint8_t region = h / 43;
    uint8_t rem = (h - region * 43) * 6;
    uint16_t p = (uint16_t)v * (255 - s) >> 8;
    uint16_t q = (uint16_t)v * (255 - ((uint16_t)s * rem >> 8)) >> 8;
    uint16_t t = (uint16_t)v * (255 - ((uint16_t)s * (255 - rem) >> 8)) >> 8;
    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Rainbow HSV→RGB conversion (8-band, evenly-spaced “rainbow”)
 *
 */
/* --------------------------------------------------------------------------
 * FastLED‑style “rainbow” HSV → RGB conversion (8‑bit, constant brightness)
 * Fixes:
 *   • proper cyan→blue fade (region 4)
 *   • exact scale8_video() behaviour (+1 fudge)
 * -------------------------------------------------------------------------- */
static inline uint8_t scale8(uint8_t i, uint8_t scale) {
    return ((uint16_t)i * scale) >> 8;        /* 0…255 → 0…255 (trunc)   */
}
static inline uint8_t scale8_video(uint8_t i, uint8_t scale) {
    /* keep one LSB of light when both inputs non‑zero                     */
    return ((uint16_t)i * scale >> 8) + ((i && scale) ? 1 : 0);
}

void hsv_to_rgb_rainbow(uint8_t hue, uint8_t sat, uint8_t val,
                        uint8_t *pr, uint8_t *pg, uint8_t *pb)
{
    /* ───── 1. coarse → fine decode of the hue byte ──────────────────── */
    uint8_t offset   = hue & 0x1F;          /* 0…31 within 1/8th          */
    uint8_t offset8  = offset << 3;         /* 0…248 (×8)                 */
    uint8_t third    = scale8(offset8,  85);/* 0…85  == ⅓ of offset8      */
    uint8_t twothird = scale8(offset8, 170);/* 0…170 == ⅔ of offset8      */

    uint8_t r=0, g=0, b=0;

    /* ───── 2. raw un‑saturated rainbow (Y‑boost, green fixes op‑in) ─── */
    const uint8_t Y1 = 1, Y2 = 0;           /* yellow brightness tweak    */
    const uint8_t G2 = 0, Gscale = 0;       /* green dimming (unused)     */

    if (!(hue & 0x80)) {                    /* 0XXX = regions 0‑3         */
        if (!(hue & 0x40)) {                /* 00XX = 0‑1                 */
            if (!(hue & 0x20)) {            /* 000X = region 0 R→O        */
                r = 255 - third;  g = third;          b = 0;
            } else {                        /* 001X = region 1 O→Y        */
                if (Y1) { r = 171;          g = 85 + third; b = 0; }
                if (Y2) { r = 170 + third;  g = 85 + twothird; b = 0; }
            }
        } else {                            /* 01XX = 2‑3                 */
            if (!(hue & 0x20)) {            /* 010X = region 2 Y→G        */
                if (Y1) { r = 171 - twothird; g = 170 + third; b = 0; }
                if (Y2) { r = 255 - offset8; g = 255;         b = 0; }
            } else {                        /* 011X = region 3 G→C        */
                r = 0;            g = 255 - third;  b = third;
            }
        }
    } else {                                /* 1XXX = regions 4‑7         */
        if (!(hue & 0x40)) {                /* 10XX = 4‑5                 */
            if (!(hue & 0x20)) {            /* 100X = region 4 C→B  **FIX** */
                r = 0;
                g = 171 - twothird;         /* fade G 171→0               */
                b =  85 + twothird;         /* rise B 85→255              */
            } else {                        /* 101X = region 5 B→P        */
                r = third;       g = 0;              b = 255 - third;
            }
        } else {                            /* 11XX = 6‑7                 */
            if (!(hue & 0x20)) {            /* 110X = region 6 P→M        */
                r =  85 + third; g = 0;      b = 171 - third;
            } else {                        /* 111X = region 7 M→R        */
                r = 170 + third; g = 0;      b =  85 - third;
            }
        }
    }

    if (G2)     g >>= 1;                    /* optional green trim        */
    if (Gscale) g = scale8_video(g, Gscale);

    /* ───── 3. apply saturation (FastLED video style) ─────────────────── */
    if (sat != 255) {
        if (sat == 0) {
            r = g = b = 255;
        } else {
            uint8_t desat   = scale8_video(255 - sat, 255 - sat);
            uint8_t satfix  = 255 - desat;          /* scale factor        */
            if (satfix) {
                r = r ? scale8_video(r, satfix) : 0;
                g = g ? scale8_video(g, satfix) : 0;
                b = b ? scale8_video(b, satfix) : 0;
            }
            r += desat;  g += desat;  b += desat;
        }
    }

    /* ───── 4. apply value/brightness (same “video” rule) ─────────────── */
    if (val != 255) {
        if (val == 0) {
            r = g = b = 0;
        } else {
            r = r ? scale8_video(r, val) : 0;
            g = g ? scale8_video(g, val) : 0;
            b = b ? scale8_video(b, val) : 0;
        }
    }

    *pr = r;   *pg = g;   *pb = b;
}


/* ─────────────────────────────────────────────────────────────────────────
 * Hue difference calculation (for smooth transitions)
 *
 */
int16_t hue_diff(uint8_t a, uint8_t b) {
    int16_t d = (int16_t)b - (int16_t)a;
    if (d > 128) d -= 256;
    else if (d < -128) d += 256;
    return d;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Generate random hue
 * -------------------------------------------------------------------------- */
uint8_t random_hue(void) {
    return (uint8_t)(rand() & 0xFF);
}









/*##############################################################################################*/
/*### RENDER ###																				*/
/*##############################################################################################*/


/* ────────────────────────────────────────────────────────────────────────
 * Load actual data for pixel into the strip buffers.
 *
 */
static inline void expand_led(uint16_t phys_idx, rgb_8b c)
{
    const size_t BYTES_PER_LED = 9;

    // compute strip & led index, then byte offset
    uint8_t  strip       = phys_idx / pixels_per_str;
    uint16_t led         = phys_idx % pixels_per_str;
    // each strip frame is (pixels_per_str*9 bytes) + 1 latch byte
    const size_t frame_bytes = (size_t)pixels_per_str * BYTES_PER_LED + 1;
    size_t offset = (size_t)strip * frame_bytes + (size_t)led * BYTES_PER_LED;

    // scale by global brightness (linear domain)
    uint8_t scaled_r = ((uint16_t)c.r); //* g_global_brightness) / 255;
    uint8_t scaled_g = ((uint16_t)c.g); //* g_global_brightness) / 255;
    uint8_t scaled_b = ((uint16_t)c.b);//* g_global_brightness) / 255;

#ifdef GAMMA_CORRECTION
    // apply gamma correction after brightness scaling
    scaled_r = gamma8[scaled_r];
    scaled_g = gamma8[scaled_g];
    scaled_b = gamma8[scaled_b];
#endif

    const uint8_t scaled[3] = { scaled_r, scaled_g, scaled_b };

    // encode bits for each channel
    uint32_t bits[3];
    for (uint8_t ch = 0; ch < 3; ++ch) {
        bits[ch] = encode_tbl[ scaled[ color_map[ch] ] ];
    }

    // guard out-of-range
    if (phys_idx >= (size_t)strip_cnt * pixels_per_str) {
#ifdef LED_DEBUG_RENDER // —————————————————————————————————————————————————————————————————————
        uint8_t hue = (uint8_t)(bits[0] >> 16);
        USBD_UsrLog("expand_led error, phys out of range! phys=%u, strip=%u, led=%u, hue=%u\n",
               (unsigned)phys_idx, (unsigned)strip, (unsigned)led, (unsigned)hue);
#endif // ——————————————————————————————————————————————————————————————————————————————————————
        return;
    }

    // write 9 bytes into strip buffer
    uint8_t *dst = &strip_buffer[offset];
    for (uint8_t ch = 0; ch < 3; ++ch) {
        dst[ch * 3 + 0] = (bits[ch] >> 16) & 0xFF;
        dst[ch * 3 + 1] = (bits[ch] >>  8) & 0xFF;
        dst[ch * 3 + 2] =  bits[ch]        & 0xFF;
    }
}


/* ────────────────────────────────────────────────────────────────────────
 * framebuffer color -> spi buffer bits
 * then actually push data out.
 */
void update_leds(void)
{
    // ─── Don’t start a new frame until ALL SPI-DMAs are free ────────────────────
    // Busy-wait here until every HAL_SPI_GetState == HAL_SPI_STATE_READY
    bool all_ready;
    do {
        all_ready = true;
        for (uint8_t s = 0; s < strip_cnt; ++s) {
            if (HAL_SPI_GetState(spi_arr[s]) != HAL_SPI_STATE_READY) {
                all_ready = false;
                break;
            }
        }
    } while (!all_ready);

#ifdef LED_DEBUG_RENDER // ───────────────────────────────────────────────────────
    // ===| MCU-side state only when debugging
    static uint32_t  ft_hist[FRAMETIME_HISTORY];
    static uint8_t   ft_idx        = 0;
    static uint8_t   ft_count      = 0;
    static uint32_t  last_ft_print = 0;

    // ===| Start the DWT timer
    uint32_t start = DWT->CYCCNT;
#endif // ───────────────────────────────────────────────────────────────────────

    // ===| Framebuffer → strip buffers → kick off DMA
    memset(strip_buffer, 0, (size_t)strip_cnt * pixels_per_str * 9 + 1);
    for (uint16_t i = 0; i < pixels_total; ++i) {
        expand_led(i, framebuffer[i]);
    }
    const size_t frame_bytes = pixels_per_str*9 + 1;
    for (uint8_t s = 0; s < strip_cnt; ++s) {
    	HAL_SPI_Transmit_DMA(spi_arr[s], &strip_buffer[s * frame_bytes], frame_bytes);
    }

#ifdef LED_DEBUG_RENDER // ───────────────────────────────────────────────────────
    // ===| Stop timing & convert to µs
    uint32_t end    = DWT->CYCCNT;
    uint32_t cycles = end - start;
    // *** use plain integer literals here, no apostrophes! ***
    uint64_t tmp    = (uint64_t)cycles * 1000000U;
    uint32_t cpu_hz = HAL_RCC_GetHCLKFreq();
    uint32_t us     = (uint32_t)(tmp / cpu_hz);

    // ===| Push into circular buffer & compute average
    ft_hist[ft_idx] = us;
    ft_idx = (ft_idx + 1) % FRAMETIME_HISTORY;
    if (ft_count < FRAMETIME_HISTORY) {
        ft_count++;
    }
    uint64_t sum = 0;
    for (uint8_t i = 0; i < ft_count; i++) {
        sum += ft_hist[i];
    }
    uint32_t avg_us = (uint32_t)(sum / ft_count);

    // ===| Print the averaged µs at up to 5 Hz
    uint32_t now = HAL_GetTick();
    if ((now - last_ft_print) >= FRAMETIME_PRINT_INTERVAL_MS) {
        USBD_UsrLog("#frametime %lu#", (unsigned long)avg_us);
        last_ft_print = now;
    }
#endif // ───────────────────────────────────────────────────────────────────────
}


/* --------------------------------------------------------------------------
 * INTERNAL HELPERS
 * -------------------------------------------------------------------------- */
static void free_buffers(void) {
	if (framebuffer) {
		free(framebuffer);
	}
	if (strip_buffer) {
		free(strip_buffer);
	}
	framebuffer = 0;
	strip_buffer = 0;
}

/* ────────────────────────────────────────────────────────────────────────
 * Neopixel encoding, table to convert our RGB into 24 bit bitstream. (3 bit timing pattern)
 * 0 bit is sent as 0b100 (short HIGH, long LOW)
 * 1 bit is sent as 0b110 (long HIGH, short LOW)
 */
static void init_encode_tbl(void) {
	for (uint16_t v = 0; v < 256; ++v) {
		uint32_t out = 0;
		for (int b = 7; b >= 0; --b) {
			out <<= 3;
			out |= ((v >> b) & 1) ? 0b110 : 0b100;
		}
		encode_tbl[v] = out;
	}
}

/* ────────────────────────────────────────────────────────────────────────
 * Color order mapping table
 * what bits represent what color.
 */
static void init_color_map(void) {
    const char *order = LED_COLOR_ORDER;
    for (int i = 0; i < 3; ++i) {
        switch (order[i]) {
            case 'R': color_map[i] = 0; break;
            case 'G': color_map[i] = 1; break;
            case 'B': color_map[i] = 2; break;
            default:  color_map[i] = 0; /* Fallback auf R */ break;
        }
    }
}


#ifdef LED_DEBUG_RENDER_HEAP
/* ────────────────────────────────────────────────────────────────────────
 * To report remaining free heap (ram)
 */
extern char _estack;
extern char _sbrk(int incr);
static size_t bytes_free_heap(void) {
	char *brk = (char*) _sbrk(0);
	return (size_t) (&_estack - brk);
}
#endif
