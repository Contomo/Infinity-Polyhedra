#ifndef CONFIG_H
#define CONFIG_H


#define LEDS_LONGEST_EDGE 24   	/* longest edge gets this many LEDs */

#define LED_COLOR_ORDER "GRB" /* LED_COLOR_ORDER */

#define EDGE_CNT 30

/* ======================================= */
// USER MAPS TO MAP EDGE TO EDGE (PHYSICAL TO VIRTUAL)
// AND ALSO DIRECTION (FACE WINDING CCW TO WIRED)

static const uint8_t USER_MAP[EDGE_CNT] = {
      10,  29,   4,  22,   0,  19,  21,  12,
      20,  17,  28,   5,  14,  27,   6,  13,
      24,   2,  15,  26,   9,  16,   3,   7,
      25,   8,   1,  18,  11,  23
};

static const bool USER_FLIP[EDGE_CNT] = {
     false, true, true, true,
     false, true, false, false,
     false, true, false, false,
     true, true, false, true,
     true, true, true, true,
     true, false, false, true,
     false, false, false, false,
     false, false
};


#define ms() HAL_GetTick()


/* ======================================= */

#define LED_DEBUG_RENDER_HEAP		/* Prints out info related to the rendering heap. */
#define LED_DEBUG_MAPPING_HEAP	/* Prints out info related to the mapping heap. */


#define LED_DEBUG_MAPPING

/* ======================================= */

#define LED_DEBUG_ANIM

#ifdef LED_DEBUG_ANIM
#define ANIMTIME_PRINT_INTERVAL_MS  200
#define ANIMTIME_HISTORY           	10
#endif

/* ======================================= */

#define LED_DEBUG_RENDER

#ifdef LED_DEBUG_RENDER
#define FRAMETIME_PRINT_INTERVAL_MS  200
#define FRAMETIME_HISTORY            10
#endif


/* ======================================= */

//#define GAMMA_CORRECTION 1.8f
#define GAMMA_CORRECTION 2.2f



// Uncomment to overwrite buffer sizes. defaults are 4k bytes and 256 bytes dropped.
#define DEBUG_TX_BUF_SIZE   4096 // 4096 = ~200 average log lines
//#define DEBUG_TX_DROP_CHUNK 256     /* drop oldest bytes on overflow */


/* Uncomment to overwrite the maximum allocation for framebuffers before it errors (sanity check sorta)
 * you need 24 bits (RGB 8 bits per color) per pixel, so 1000 pixel would need 3kbytes.
 * default limit is 16 kbytes
 */
//#define LED_RENDER_MAX_ALLOC (16 * 1024) /* MAX ALLOCATION FOR BUFFERS */


#endif
