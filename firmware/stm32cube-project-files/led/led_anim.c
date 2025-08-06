/* --------------------------------------------------------------------------
 * led_anim.c – LED animation logic using topology and geometry
 * -------------------------------------------------------------------------- */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "polyhedron.h"
#include "led_mapping.h"         /* mapping_* getters */
#include "led_render.h"          /* set_all_pixels_color, add_pixel_color, update_leds */
#include "led_anim.h"
#include <time.h>

#include "led_debug.h"

#include "config.h"

/* External polyhedron instance (created in main.c) */
extern Polyhedron poly;




typedef struct { float x,y,z; } Vec3;
static Vec3 *led_pos = NULL;        /* len = mapping_get_total_pixels() */

static bool build_led_pos_cache()
{
    if (led_pos) return true;                                        /* already done */
    uint16_t tot = mapping_get_total_pixels();
    led_pos = malloc(sizeof(Vec3) * tot);
    if (!led_pos) return false;                                       /* OOM – bail */

    const EdgeLedInfo *ei = mapping_get_edge_info();
    for (uint8_t e = 0; e < poly.E; ++e) {
        const float *A = poly.v[ poly.e[e].a ];
        const float *B = poly.v[ poly.e[e].b ];
        for (uint16_t i = 0; i < ei[e].count; ++i) {
            float t  = (ei[e].count>1) ? (float)i/(ei[e].count-1) : 0.f;
            float x  = A[0] + (B[0]-A[0])*t;
            float y  = A[1] + (B[1]-A[1])*t;
            float z  = A[2] + (B[2]-A[2])*t;
            uint16_t phys = ei[e].start + i*ei[e].step;
            led_pos[phys] = (Vec3){x,y,z};
        }
    }
    return true;
}


/* ##################################################################################################### */
#ifdef LED_DEBUG_ANIM // ───────────────────────────────────────────────────────────────
#include "usb_comms.h"    // for USBD_UsrLog()
#include "stm32f4xx.h"    // for DWT->CYCCNT & HAL_RCC_GetHCLKFreq()
#include <stdint.h>

// MCU‐side rolling‐average state for animation prep times
static uint32_t anim_hist[ANIMTIME_HISTORY];
static uint8_t  anim_idx        = 0;
static uint8_t  anim_count      = 0;
static uint32_t last_anim_print = 0;
static uint32_t start = 0;
#endif // ─────────────────────────────────────────────────────────────────────────────

void anim_time_start(void){
#ifdef LED_DEBUG_ANIM // ───────────────────────────────────────────────────────────
    // ===| Start DWT timing for animation prep
    start = DWT->CYCCNT;
#endif // ─────────────────────────────────────────────────────────────────────────
}

void anim_time_end(void){
#ifdef LED_DEBUG_ANIM // ───────────────────────────────────────────────────────────
    // ===| Stop timing & convert to µs
    uint32_t end    = DWT->CYCCNT;
    uint32_t cycles = end - start;
    uint64_t tmp    = (uint64_t)cycles * 1000000U;
    uint32_t cpu_hz = HAL_RCC_GetHCLKFreq();
    uint32_t us     = (uint32_t)(tmp / cpu_hz);

    // ===| Push into circ‐buffer & compute average
    anim_hist[anim_idx] = us;
    anim_idx = (anim_idx + 1) % ANIMTIME_HISTORY;
    if (anim_count < ANIMTIME_HISTORY) {
        anim_count++;
    }
    uint64_t sum = 0;
    for (uint8_t j = 0; j < anim_count; j++) {
        sum += anim_hist[j];
    }
    uint32_t avg_us = (uint32_t)(sum / anim_count);

    // ===| Print the averaged µs at up to 5 Hz
    uint32_t now = HAL_GetTick();
    if ((now - last_anim_print) >= ANIMTIME_PRINT_INTERVAL_MS) {
        USBD_UsrLog("#animtime %lu#", (unsigned long)avg_us);
        last_anim_print = now;
    }
#endif // ─────────────────────────────────────────────────────────────────────────
}
/* ##################################################################################################### */





/* #################################################################################################### */
/* ### TIMEKEEPING / MATH HELPERS ##################################################################### */
/* #################################################################################################### */


static inline float time_delta(uint32_t *last_ms) {
    uint32_t now = ms();
    float    dt  = *last_ms ? (now - *last_ms) * 0.001f : 0.0f;
    *last_ms      = now;
    return dt;
}


// fast approximation of x^y
static inline float fast_powf(float x, float y) {
    // --- 1) extract exponent and mantissa of x
    union { float f; uint32_t i; } ux = { x };
    int    ex = (int)((ux.i >> 23) & 0xFF) - 127;
    float  mant = (ux.i & 0x7FFFFF) / 8388608.0f + 1.0f;

    // --- 2) approximate log2(x) = ex + log2(mant)
    //    use a 3rd-order Taylor around mant ≈ 1:
    float   f1 = mant - 1.0f;
    float   log2_m = f1
                    - 0.5f * f1*f1
                    + (1.0f/3.0f) * f1*f1*f1;
    float   log2x = ex + log2_m;

    // --- 3) scale by y to get z = y * log2(x)
    float z = y * log2x;
    int   iz = (int)floorf(z);
    float frac = z - (float)iz;      // fractional part

    // --- 4) approximate 2^frac on [0,1) with a quadratic
    //     2^f ≈ 1 + f·ln2 + f²·(ln2)²/2
    const float LN2 = 0.69314718f;
    float exp2_frac = 1.0f
                    + frac * LN2
                    + frac*frac * (LN2*LN2 * 0.5f);

    // --- 5) rebuild result = 2^iz * exp2_frac
    int  ez = iz + 127;
    if (ez <= 0) return 0.0f;        // underflow
    if (ez >= 255) return INFINITY;  // overflow
    union { uint32_t i; float f; } uz = { (uint32_t)ez << 23 };
    return uz.f * exp2_frac;
}



/* #################################################################################################### */
/* ### FRAME / HUE / ANIMATION HELPERS ################################################################ */
/* #################################################################################################### */


/**
 * Test-Funktion: Gradient entlang jeder Kante,
 * Hue aus vertex_hue_from_xyz + hue_offset,
 * mit korrekter Richtung (rev) aus edge_info.
 */
// Kürzeste Differenz auf dem Kreis 0…255


/* ========================================================================================== */

static void fade_frame(uint8_t fade_amt, uint8_t power /* 1…8 ≈ power */) {
    // invert so 0 = no fade, 255 = full fade
    uint8_t factor_q8 = 255 - fade_amt;
    uint16_t tot = mapping_get_total_pixels();
    for (uint16_t i = 0; i < tot; ++i) {
        rgb_8b c = framebuffer[i];
        /* scale each channel with (v * f)^γ  in 8-bit integer space          */
        uint16_t r = (c.r * factor_q8) >> 8;
        uint16_t g = (c.g * factor_q8) >> 8;
        uint16_t b = (c.b * factor_q8) >> 8;
        for (uint8_t k = 1; k < power; ++k) { r = (r * factor_q8) >> 8;
                                              g = (g * factor_q8) >> 8;
                                              b = (b * factor_q8) >> 8; }
        framebuffer[i].r = (uint8_t)r;
        framebuffer[i].g = (uint8_t)g;
        framebuffer[i].b = (uint8_t)b;
    }
}

/* ========================================================================================== */

// Returns a random LED index in [0..pixels_total-1]
uint16_t random_pixel_index(void) {
    // 1) PRNG state lives here, initialized non-zero on first call
    static uint32_t state = 0xA5A5A5A5;

    // 2) xorshift32 step
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;

    // 3) map into your LED count
    uint16_t total = mapping_get_total_pixels();
    if (total == 0) return 0;
    return (uint16_t)(x % total);
}

/* ========================================================================================== */


static inline void face_index_to_rgb(uint8_t face,
                                     uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* evenly spaced around the colour-wheel */
    uint8_t hue = (uint8_t)((uint16_t)face * 255u / poly.F);
    hsv_to_rgb_rainbow(hue, 255, 255, r, g, b);
}





/* ========================================================================================== */

/* #################################################################################################### */
/* ANIMATIONS ######################################################################################### */
/* #################################################################################################### */


void vertex_hsv_from_xyz(const float v[3],
                                uint8_t *out_hue,
                                uint8_t *out_sat,
                                uint8_t *out_val)
{
    // 1) Azimut (XY-Ebene) → Hue 0…1
    float az   = atan2f(v[1], v[0]);
    float huef = (az + M_PI) / (2.0f * M_PI);
    // 2) Z-Höhe (–1…1) → Saturation 0…1
    float satf = (v[2] + 1.0f) * 0.5f;
    // 3) Value fest auf voll
    float valf = 1.0f;

    *out_hue = (uint8_t)(huef * 255.0f + 0.5f);
    *out_sat = (uint8_t)(satf * 255.0f + 0.5f);
    *out_val = (uint8_t)(valf * 255.0f + 0.5f);
}

void vertex_hv_from_xyz(const float v[3],
                                uint8_t *out_hue,
                                uint8_t *out_val)
{
    // 1) XY-Azimuth → 0…1
    float az   = atan2f(v[1], v[0]);
    float hueXY = (az + (float)M_PI) / (2.0f * (float)M_PI);

    // 2) Pitch (Z-Differenz) → 0…1
    float r_xy  = sqrtf(v[0]*v[0] + v[1]*v[1]);
    float pitch = atan2f(v[2], r_xy);
    float hueZ  = (pitch + (float)M_PI/2.0f) / (float)M_PI;

    // 3) Kombiniertes Hue und Helligkeit
    float combined_hue = hueXY * 0.7f + hueZ * 0.3f;
    float brightness   = 0.5f + 0.5f * sinf(v[2] * ((float)M_PI/2.0f));

    *out_hue = (uint8_t)(combined_hue * 255.0f + 0.5f);
    *out_val = (uint8_t)(brightness   * 255.0f + 0.5f);
}


void vertex_hue_from_z(const float v[3],
                       uint8_t *out_hue,
                       uint8_t *out_sat,
                       uint8_t hue_offset)
{
    // map Z ∈ [–1…+1] → [0…255]
    float nz = (v[2] + 1.0f) * 0.5f;
    uint8_t hue = (uint8_t)(nz * 255.0f + 0.5f);

    // map angle ∈ [0…1] → saturation
    float angle = atan2f(v[1], v[0]);
    float norm = (angle + M_PI) / (2.0f * M_PI);
    uint8_t sat = (uint8_t)(norm * 255.0f + 0.5f);

    *out_hue = hue + hue_offset;
    *out_sat = sat;
}


void vertex_hue_from_spherical(const float v[3],
                               uint8_t *out_hue,
                               uint8_t *out_sat,
                               uint8_t hue_offset)
{
    // radial distance
    float r = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (r == 0.0f) {
        *out_hue = hue_offset;
        *out_sat = 0;
        return;
    }

    // longitude → hue
    float theta = atan2f(v[1], v[0]);            // –π…+π
    float norm_h = (theta + M_PI) / (2.0f*M_PI); // 0…1
    uint8_t hue = (uint8_t)(norm_h * 255.0f + 0.5f);

    // latitude → saturation
    float phi = acosf(v[2] / r);    // 0…π
    float norm_s = phi / M_PI;      // 0…1
    uint8_t sat = (uint8_t)(norm_s * 255.0f + 0.5f);

    *out_hue = hue + hue_offset;    // wraps modulo 256
    *out_sat = sat;
}


void vertex_hue_from_xyz(const float v[3], uint8_t *out_hue, uint8_t hue_offset)
{
    // 1) compute base hue from XY-angle
    float angle = atan2f(v[1], v[0]);                 // –π … +π
    float norm  = (angle + (float)M_PI) / (2.0f * (float)M_PI);
    uint8_t base = (uint8_t)(norm * 255.0f + 0.5f);

    // 2) add offset (wraps modulo 256 via uint8_t)
    *out_hue = (uint8_t)(base + hue_offset);
}

/**
 * @brief Mappt eine 3D-Position auf RGB und rotiert die Kanäle um offset.
 * @param v      Position im Raum, normalisiert ([-1…+1] pro Achse)
 * @param offset Zyklischer Kanal-Offset (0→RGB, 1→GBR, 2→BRG)
 * @param outR, outG, outB Pointer für Ausgabe 0…255
 */
/**
 * Mappt eine 3D-Position auf RGB-Kanäle und rotiert die Kanäle um offset:
 *   offset = 0 → R=X, G=Y, B=Z
 *   offset = 1 → R=Y, G=Z, B=X
 *   offset = 2 → R=Z, G=X, B=Y
 */
static void get_rgb_from_xyz(const float v[3],
                             uint8_t      offset,
                             uint8_t     *outR,
                             uint8_t     *outG,
                             uint8_t     *outB)
{
    uint8_t ch[3];
    for (int i = 0; i < 3; ++i) {
        float f = (v[i] + 1.0f) * 0.5f;        // von [-1..+1] → [0..1]
        ch[i]   = (uint8_t)(f * 255.0f + 0.5f);
    }
    switch (offset % 3) {
        case 0: *outR = ch[0]; *outG = ch[1]; *outB = ch[2]; break;
        case 1: *outR = ch[1]; *outG = ch[2]; *outB = ch[0]; break;
        default:*outR = ch[2]; *outG = ch[0]; *outB = ch[1]; break;
    }
}



/* ────────────────────────────────────────────────────────────────────────── */
/* BASIC API                                                                */
/* ────────────────────────────────────────────────────────────────────────── */
void blackout_all_pixels(void)
{
    set_all_pixels_color(0, 0, 0);
    update_leds();
}

/* ────────────────────────────────────────────────────────────────────────── */
/* FULL VERTEX PALETTE (GRADIENT ALONG EVERY EDGE)                          */
/* ────────────────────────────────────────────────────────────────────────── */
// -----------------------------------------------------------------------------



/**
 * Logischer Farbgradient entlang jeder Kante:
 * - Hue per vertex_hue_from_xyz()
 * - Offset addierbar
 * - Alle LEDs physikalisch in aufsteigender Reihenfolge (start + i)
 * - Der Start-Hue wird getauscht, wenn rev==true, damit auch verdrehte
 *   Verkabelung immer logisch A→B wiedergibt.
 */
void show_vertex_palette_xyz(uint8_t sat,
                             uint8_t val,
                             uint8_t hue_offset)
{
	g_global_brightness = 200;
    set_all_pixels_color(0, 0, 0);
    anim_time_start();
    const EdgeLedInfo *info = mapping_get_edge_info();
    uint8_t            E    = poly_edge_count(&poly);

    for (uint8_t e = 0; e < E; ++e) {
        EdgeLedInfo inf = info[e];
        Edge        edge = poly_get_edge(&poly, e);

        // 1) Raw hues at the endpoints
        uint8_t raw_hA, raw_hB;
        vertex_hue_from_xyz(poly.v[edge.a], &raw_hA, hue_offset);
        vertex_hue_from_xyz(poly.v[edge.b], &raw_hB, hue_offset);

        // 2) Compute signed float delta, handling wrap
        float hA = raw_hA;
        float hB = raw_hB;
        float dh = hB - hA;
        if (dh > 128.0f)  dh -= 256.0f;
        else if (dh < -128.0f) dh += 256.0f;

        // 3) Walk the LEDs with float interpolation
        for (uint16_t i = 0; i < inf.count; ++i) {
            uint16_t phys = inf.start + i * inf.step;
            // t ∈ [0..1]
            float t = (inf.count > 1)
                    ? (float)i / (float)(inf.count - 1)
                    : 0.0f;
            // hue_f ∈ [hA..hA+dh]
            float hue_f = hA + dh * t;
            // wrap back into [0..256)
            uint8_t h = (uint8_t)fmodf(hue_f + 256.0f, 256.0f);

            uint8_t R, G, B;
            hsv_to_rgb_rainbow(h, sat, val, &R, &G, &B);
            add_pixel_color(phys, R, G, B);
        }

    }
    anim_time_end();

    update_leds();
}





void show_vertex_gradient(uint8_t vertex,
                          uint8_t sat,
                          uint8_t val,
                          uint8_t hue_offset)
{
    // 1) clear all
    set_all_pixels_color(0, 0, 0);
    g_global_brightness = 200;

    anim_time_start();

    // 2) grab edge‐LED layout
    const EdgeLedInfo *info = mapping_get_edge_info();
    uint8_t            E    = poly_edge_count(&poly);

    // 3) unit direction from origin → chosen vertex
    const float *dir_v = poly.v[vertex];
    float mag = sqrtf(dir_v[0]*dir_v[0]
                    + dir_v[1]*dir_v[1]
                    + dir_v[2]*dir_v[2]);
    if (mag == 0.0f) return;  // avoid div0

    // 4) for each edge…
    for (uint8_t e = 0; e < E; ++e) {
        EdgeLedInfo inf = info[e];
        Edge        edge = poly_get_edge(&poly, e);
        const float *A   = poly.v[edge.a];
        const float *B   = poly.v[edge.b];

        // 5) for each LED on this edge…
        for (uint16_t i = 0; i < inf.count; ++i) {
            // 5a) lerp world‐space position
            float t = (inf.count > 1)
                    ? ((float)i / (float)(inf.count - 1))
                    : 0.0f;
            float px = A[0] + (B[0] - A[0]) * t;
            float py = A[1] + (B[1] - A[1]) * t;
            float pz = A[2] + (B[2] - A[2]) * t;

            // 5b) project onto dir_v, normalize → dp∈[–1…+1]
            float dp = (px*dir_v[0] + py*dir_v[1] + pz*dir_v[2]) / mag;
            if      (dp < -1.0f) dp = -1.0f;
            else if (dp > +1.0f) dp = +1.0f;

            // 5c) map dp→ hue in [0…255] with integer‐style rounding
            //     raw_h = round( (dp + 1)/2 * 255 )
            float  scaled = (dp + 1.0f) * 0.5f * (1+(float)hue_offset/40) * 255.0f;
            uint8_t raw_h = (uint8_t)(scaled + 0.5f);


            // apply hue offset (wraps mod 256)
            //uint8_t h     = raw_h + hue_offset;
            uint8_t h     = raw_h;
            // 5d) physical index via step (precomputed +1 or -1)
            uint16_t phys = inf.start + i * inf.step;

            // 5e) HSV→RGB & set  (val handled in expand_led())
            uint8_t R, G, B;
            hsv_to_rgb_rainbow(h, sat, val, &R, &G, &B);
            add_pixel_color(phys, R, G, B);
        }
    }

    anim_time_end();

    // 6) push to strips
    update_leds();
}




/**
 * @brief Zeichnet jeden Edge mit RGB-Färbung aus interpolierten XYZ-Punkten.
 * @param offset Zyklischer Kanal-Offset (0..2)
 */
void show_vertex_palette_index(uint8_t sat, uint8_t val, uint8_t hue_offset) {
	set_all_pixels_color(0, 0, 0);
	anim_time_start();
    const EdgeLedInfo *info = mapping_get_edge_info();
    uint8_t            E    = poly_edge_count(&poly);
    uint8_t            V    = poly.V;  // total vertices

    for (uint8_t e = 0; e < E; ++e) {
        EdgeLedInfo inf = info[e];
        Edge        edge = poly_get_edge(&poly, e);

        // compute the two endpoint hues
        uint8_t raw_hA = (uint8_t)(((uint16_t)edge.a * 255u) / V + hue_offset);
        uint8_t raw_hB = (uint8_t)(((uint16_t)edge.b * 255u) / V + hue_offset);

        // if the strip is physically flipped, swap so logical A→B still flows correctly
        uint8_t hStart = raw_hA;
        uint8_t hEnd   = raw_hB;
        if (inf.step < 0) {
            // swap if we’re going B→A
            uint8_t tmp = hStart;
            hStart = hEnd;
            hEnd   = tmp;
        }
        int16_t dh = hue_diff(hStart, hEnd);

        // step along the LEDs
        for (uint16_t i = 0; i < inf.count; ++i) {
            // linear parameter 0..1
            float t = (inf.count > 1)
                    ? ((float)i / (float)(inf.count - 1))
                    : 0.0f;
            // interpolated hue
            uint8_t h = (uint8_t)(hStart + dh * t + 0.5f);

            // physical index, honoring rev
            uint16_t phys = inf.start + i * inf.step;

            // convert & set
            uint8_t R, G, B;
            hsv_to_rgb_rainbow(h, sat, val, &R, &G, &B);
            add_pixel_color(phys, R, G, B);
        }
    }
    anim_time_end();
    update_leds();
}





/* ────────────────────────────────────────────────────────────────────────── */
/* SHOW A SINGLE FACE IN ITS “NICE” RGB                                     */
/* ────────────────────────────────────────────────────────────────────────── */
void show_face(uint8_t f)
{
    set_all_pixels_color(0, 0, 0);

    uint8_t r, g, b; face_index_to_rgb(f, &r, &g, &b);
    const PixelMapping *pm   = mapping_get_map();
    uint16_t            tot  = mapping_get_total_pixels();

    for (uint8_t i = 0; i < poly.fv[f]; ++i) {
        uint8_t v0 = poly.f[f][i];
        uint8_t v1 = poly.f[f][(i + 1) % poly.fv[f]];
        uint8_t le = poly_find_edge(&poly, v0, v1);

        /* colour every pixel whose mapping.edge == le */
        for (uint16_t idx = 0; idx < tot; ++idx)
            if (pm[idx].edge == le)
                add_pixel_color(pm[idx].phys, r, g, b);
    }
    update_leds();
}

/* ==========================================================================
 * EXTRA DEMO ANIMATIONS
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Rainbow cycle (hue moves over time across all pixels)
 * -------------------------------------------------------------------------- */
static uint8_t rainbow_offset = 0;
void anim_rainbow_tick(void)
{
    const PixelMapping *pm = mapping_get_map();
    uint16_t total = mapping_get_total_pixels();

    for (uint16_t i = 0; i < total; ++i) {
        uint8_t hue = (uint8_t)( ( (uint32_t)i * 256 / total + rainbow_offset) & 0xFF );
        uint8_t r,g,b; hsv_to_rgb_rainbow(hue, 255, 120, &r,&g,&b);
        set_pixel_color(pm[i].phys, r, g, b);
    }
    update_leds();

    rainbow_offset += 1;  /* speed: higher = faster */
}

/* --------------------------------------------------------------------------
 * Breathing white glow (global brightness pulse)
 * -------------------------------------------------------------------------- */
void anim_breath_tick(void)
{
    float phase = (sin( (float)HAL_GetTick() * 0.002f ) + 1.0f) * 0.5f;  /* 0..1 */
    uint8_t v = (uint8_t)(phase * 255);

    set_all_pixels_color(v, v, v);
    update_leds();
}

/* --------------------------------------------------------------------------
 * Twinkle – random pixels sparkle
 * -------------------------------------------------------------------------- */
void anim_twinkle_tick(void)
{
    const PixelMapping *pm = mapping_get_map();
    uint16_t total = mapping_get_total_pixels();

    /* fade all pixels slightly */
    for (uint16_t i = 0; i < total; ++i) {
        uint8_t r,g,b;
        get_pixel_color(pm[i].phys, &r,&g,&b); // DOESNT EXIST
        r = (uint8_t)(r * 0.9f);
        g = (uint8_t)(g * 0.9f);
        b = (uint8_t)(b * 0.9f);
        add_pixel_color(pm[i].phys, r,g,b);
    }

    /* randomly pick 5% of LEDs to flash */
    for (uint16_t k = 0; k < total/20; ++k){
        uint16_t idx = rand() % total;
        uint8_t r,g,b; hsv_to_rgb_rainbow(rand() & 0xFF, 200, 255, &r,&g,&b);
        add_pixel_color(pm[idx].phys, r,g,b);
    }
    update_leds();
}



/* ====================================================================================================================================================
 * ------[ PLASMA SWIRL
 * ==================================================================================================================================================== */
static float plasma_phase = 0.f;
float K1=4.3f, K2=2.7f, K3=3.7f; /* spatial frequencies */
float speed = 0.015f;            /* radians per frame   */

void anim_plasma_swirl_tick(void)
{
    if(!build_led_pos_cache()){
    	return;
    }

    g_global_brightness = 200;

    uint16_t tot = mapping_get_total_pixels();
    for (uint16_t p=0; p<tot; ++p){
        Vec3 v = led_pos[p];
        float n =  sinf(K1*v.x + plasma_phase)
                 + sinf(K2*v.y + plasma_phase*0.8f)
                 + sinf(K3*v.z + plasma_phase*1.3f);
        /* clamp & map [-3..+3] → [0..255] */
        uint8_t hue = (uint8_t)(((n + 3.f) * 42.5f));   /* 255/6 ≈ 42.5 */
        uint8_t r,g,b; hsv_to_rgb_rainbow(hue, 255, 180, &r,&g,&b);
        set_pixel_color(p, r,g,b);
    }
    plasma_phase += speed;
    update_leds();
}



/* ====================================================================================================================================================
 * ------[ SHOOTING STARS
 * ==================================================================================================================================================== */

// Configuration (tweak as desired)
static volatile uint8_t NUM_STARS = 13;
static volatile uint8_t TAIL_LEN = 5;
static volatile uint8_t STAR_SPEED = 1;  // LEDs per animation tick

bool initialized_stars = false;

// Per-star state
typedef struct {
    uint8_t  edge;        // current edge
    uint8_t  prev_edge;   // previous edge (for tail spill)
    bool     dir;         // false=A→B, true=B→A
    bool     prev_dir;    // direction on prev_edge
    int16_t  pos;         // head position on current edge
} Star;

// Static storage
static Star stars[30];
/* -------------------------------------------------------------------------- */
// Initialize stars: random start edges & directions
void init_shooting_stars(void) {
	if (initialized_stars == true){
		return;
	}
    const uint8_t E = poly.E;
    const EdgeLedInfo *info = mapping_get_edge_info();
    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].edge      = rand() % E;
        stars[i].prev_edge = stars[i].edge;
        stars[i].dir       = rand() & 1;
        stars[i].prev_dir  = stars[i].dir;
        // start just off the tail, so they “enter” cleanly
        uint16_t cnt = info[ stars[i].edge ].count;
        uint16_t offset = rand() % cnt;                 /* random start */
        stars[i].pos = (int16_t)offset;
    }
    initialized_stars = true;
}
/* -------------------------------------------------------------------------- */

// returns true if any star is currently sitting on edge `e`
static bool edge_is_occupied(uint8_t e) {
    for (int i = 0; i < NUM_STARS; ++i) {
        if (stars[i].edge == e) return true;
    }
    return false;
}

/*
 * Pick a new edge at vertex v, excluding the edge we came from.
 * Prefer edges with no star on them right now.
 *
 * Uses reservoir sampling to choose uniformly at random
 * without storing all candidates in an array.
 */
static uint8_t pick_next_edge(uint8_t v, uint8_t exclude_edge) {
    uint8_t choice;
    int     count = 0;

    // 1) Try to pick among *free* edges
    for (uint8_t e = 0; e < poly.E; ++e) {
        if (e == exclude_edge) continue;
        Edge ed = poly.e[e];
        if ((ed.a != v && ed.b != v) || edge_is_occupied(e))
            continue;
        // reservoir: each candidate *could* become the choice with prob 1/count
        if (rand() % (++count) == 0) {
            choice = e;
        }
    }
    if (count > 0) {
        return choice;
    }

    // 2) No free edges? pick among *busy* edges instead
    count = 0;
    for (uint8_t e = 0; e < poly.E; ++e) {
        if (e == exclude_edge) continue;
        Edge ed = poly.e[e];
        if (ed.a != v && ed.b != v) continue;
        if (!edge_is_occupied(e)) continue;
        if (rand() % (++count) == 0) {
            choice = e;
        }
    }
    // if still nothing (shouldn’t happen), fall back
    return (count > 0) ? choice : exclude_edge;
}
/* -------------------------------------------------------------------------- */
// Animation tick: call this from your main loop
void anim_shooting_stars_tick(void) {
    // 1) clear frame
	init_shooting_stars();
	fade_frame(50, 2);
    anim_time_start();
    const EdgeLedInfo *info = mapping_get_edge_info();

    // 2) advance & draw each star
    for (int i = 0; i < NUM_STARS; ++i) {
        Star *S = &stars[i];
        EdgeLedInfo inf = info[S->edge];
        uint16_t leds = inf.count;

        // advance head
        S->pos += (S->dir ? -STAR_SPEED : STAR_SPEED);
        bool crossed = (S->pos < 0 || S->pos >= (int)leds);

        if (crossed) {
            /* remember where we came from *before* we change anything       */
            S->prev_edge = S->edge;
            S->prev_dir  = S->dir;
            // determine arrival vertex
            Edge ed = poly.e[S->edge];
            uint8_t arrived = S->dir ? ed.a : ed.b;
            // pick next edge & direction
            uint8_t next = pick_next_edge(arrived, S->edge);
            Edge ne = poly.e[next];
            // determine logical direction along new edge
            S->dir = (ne.b == arrived);
            S->edge = next;
            // reset pos just beyond start
            //S->pos  = S->dir ? leds - 1 + STAR_SPEED : -STAR_SPEED;
            uint16_t cnt2 = info[next].count;
            S->pos  = S->dir ? (cnt2 - 1) : 0;        /* no overshoot? */
        }
        /* draw tail – may span current *and* previous edge */
        const EdgeLedInfo *inf_cur  = &info[S->edge];
        const EdgeLedInfo *inf_prev = &info[S->prev_edge];
        // draw tail behind head
        for (int t = 0; t < TAIL_LEN; ++t) {
            int16_t p = S->pos + (S->dir ? +t : -t);
            uint8_t bright = 255 * (TAIL_LEN - t) / TAIL_LEN;

            /* 1) on current edge? */
            if (p >= 0 && p < (int)inf_cur->count) {
                uint16_t phys = inf_cur->start + p * inf_cur->step;
                add_pixel_color(phys, bright, bright, bright);
            }
            /* 2) overspill onto previous edge */
            else {
                int16_t pp = p;
                /* map p into [0..count-1] of previous edge respecting dir */
                if (S->prev_dir)  pp += inf_prev->count;   /* came from A→B */
                else              pp -= inf_prev->count;   /* came from B→A */
                if (pp >= 0 && pp < (int)inf_prev->count) {
                    uint16_t phys = inf_prev->start + pp * inf_prev->step;
                    add_pixel_color(phys, bright, bright, bright);
                }
            }
        }
    }
    anim_time_end();

    // 3) push to strips
    update_leds();
}


/*

		typedef struct {
		  const char * name;
		  MinefieldSettings data;
		} Preset;

		static Preset minefield_presets[] = {
		  { "default",      { 1.2f, 0.015f, 0.3f, 241, 2.3f } },
		  { "deep_slow",    { 0.5f, 0.010f, 0.5f, 200, 3.0f } },
		  { "fast_shallow", { 3.0f, 0.020f, 0.1f, 255, 1.0f } },
		};
		static const size_t minefield_preset_count =
		  sizeof(minefield_presets)/sizeof(*minefield_presets);


typedef void (*AnimTickFn)(void);

typedef struct {
  const char     *name;
  AnimTickFn      tick;
  ParamDef       *params;
  size_t          param_count;
} Animation;

static Animation animations[] = {
  { "minefield", anim_minefield_tick, minefield_params, minefield_param_count },
  // { "plasma",    anim_plasma_swirl_tick,    plasma_params, plasma_count },
  // …more…
};
static const size_t animation_count = sizeof(animations)/sizeof(*animations);


typedef enum { PT_FLOAT, PT_UINT8 } ParamType;

typedef struct {
  const char *name;
  const char *desc;
  ParamType   type;
  void       *ptr;    // &minefield.wave_speed, etc.
  float       min, max;
} ParamDef;

static ParamDef minefield_params[] = {
  { "burst_hz",        "Explosions per second",        PT_FLOAT,  &minefield.burst_hz,        0.1f, 10.0f },
  { "wave_speed",      "Wave speed (units per tick)",  PT_FLOAT,  &minefield.wave_speed,      0.001f, 1.0f },
  { "shell_thickness", "Shell half-width",             PT_FLOAT,  &minefield.shell_thickness, 0.0f,   1.0f },
  { "fade_amount",     "Fade amount (0–255)",          PT_UINT8,  &minefield.fade_amount,     0,     255   },
  { "falloff_exp",     "Falloff exponent",             PT_FLOAT,  &minefield.falloff_exp,     0.5f,  8.0f  },
};
static const size_t minefield_param_count = sizeof(minefield_params)/sizeof(*minefield_params);
*/

/* ====================================================================================================================================================
 * ------[ Minefield shockwave
 * ==================================================================================================================================================== */

#define MAX_CONCURRENT_EXPLOSIONS 20
#define POLY_RADIUS              2.0f  // normalized polyhedron radius

#define PALETTE_SIZE			16

// User-tweakable settings struct, with randomization ranges
typedef struct {
    float   expl_per_sec;         // explosions per second
    float   shell_speed;          // mean units per second
    float   shell_speed_rng;      // +/- range for speed
    float   shell_thickness;      // mean half-width
    float   shell_thickness_rng;  // +/- range for half-width
    uint8_t fade_amount;          // frame fade (0–255)
    float   falloff_exp;          // spatial exponent for shell edge
    float   radial_falloff_exp;   // exponent for distance fade
    uint8_t palette[PALETTE_SIZE];          // hue values for explosions;               // explosion lifetime in seconds
} MinefieldSettings;

static MinefieldSettings minefield = {
    .expl_per_sec            = 0.35f,
    .shell_speed             = 0.25f,
    .shell_speed_rng         = 0.1f,
    .shell_thickness         = 0.3f,
    .shell_thickness_rng     = 0.15f,
    .fade_amount             = 11,
    .falloff_exp             = 2.1f,
    .radial_falloff_exp      = 2.2f, //1.3
    .palette                 = { 240, 136, 46, 47, 48, 243, 237, 165,160 } //240 - 245 //46 - 48
};


//    .palette = { 232, 168, 46 },  // Neon Pink, Electric Blue, Acid Yellow
//    .palette = { 240, 176, 136, 4 },  // Hot Pink, Deep Blue, Aqua, Vivid Red
//    .palette = { 248, 170, 46, 8 },  // Magenta, Cool Blue, Neon Yellow, Bright Red
//    .palette = { 208, 136, 48 },  // Purple, Aqua, Lime
//    .palette = { 240, 168, 8 },  // Hot Pink, Electric Blue, Bright Red


// Explosion state with per-instance parameters and age
typedef struct {
    bool     active;
    Vec3     center;
    float    radius;
    float    speed;
    float    thickness;

    uint8_t  hue;
    float    min_r2;
    float    max_r2;
} Explosion;

extern uint8_t debug_hue;

static Explosion explosions[MAX_CONCURRENT_EXPLOSIONS];

// helper to pick a random value in [base-range, base+range]
static inline float rand_range(float base, float range) {
    return base + range * ((rand()/(float)RAND_MAX) * 2.0f - 1.0f);
}

// Spawn a new explosion with randomized speed and thickness
static void spawn_explosion(void) {
    for (int i = 0; i < MAX_CONCURRENT_EXPLOSIONS; ++i) {
        Explosion *xpl = &explosions[i];
        if (!xpl->active) {
        	if(debug_hue != 0){
        		for (int p_idx; p_idx < PALETTE_SIZE; p_idx++){
        		        		minefield.palette[p_idx] = 0;
        		        	}
        		        	// Select palette based on debug_hue (0-255)
        		        	if (debug_hue < 51) {
        		        	    minefield.palette[0] = 232;  // Neon Pink, Electric Blue, Acid Yellow
        		        	    minefield.palette[1] = 168;
        		        	    minefield.palette[2] = 46;
        		        	} else if (debug_hue < 102) {
        		        	    minefield.palette[0] = 240;  // Hot Pink, Deep Blue, Aqua, Vivid Red
        		        	    minefield.palette[1] = 176;
        		        	    minefield.palette[2] = 136;
        		        	    minefield.palette[3] = 4;
        		        	} else if (debug_hue < 153) {
        		        	    minefield.palette[0] = 248;  // Magenta, Cool Blue, Neon Yellow, Bright Red
        		        	    minefield.palette[1] = 170;
        		        	    minefield.palette[2] = 46;
        		        	    minefield.palette[3] = 8;
        		        	} else if (debug_hue < 204) {
        		        	    minefield.palette[0] = 208;  // Purple, Aqua, Lime
        		        	    minefield.palette[1] = 136;
        		        	    minefield.palette[2] = 48;
        		        	} else {
        		        	    minefield.palette[0] = 240;  // Hot Pink, Electric Blue, Bright Red
        		        	    minefield.palette[1] = 168;
        		        	    minefield.palette[2] = 8;
        		        	}
        	}



            uint16_t idx = random_pixel_index();
            xpl->center    = led_pos[idx];
            xpl->radius    = 0.0f;
            xpl->speed     = rand_range(minefield.shell_speed, minefield.shell_speed_rng);
            xpl->thickness = rand_range(minefield.shell_thickness, minefield.shell_thickness_rng);
            if (xpl->thickness < 0.0f) xpl->thickness = 0.0f;

            do {xpl->hue = minefield.palette[rand() % PALETTE_SIZE];
            } while (xpl->hue == 0);

            xpl->active    = true;
            xpl->min_r2    = 0.0f;
            xpl->max_r2    = xpl->thickness * xpl->thickness;
            break;
        }
    }
}

void anim_minefield_tick(void) {
    if (!build_led_pos_cache()) return;

    // timing
    uint32_t now = ms();
    static uint32_t last_burst_ms = 0, last_frame_ms = 0;
    float dt_s = time_delta(&last_frame_ms);

    // fade and timing
    fade_frame(minefield.fade_amount, 2);
    anim_time_start();

    // spawn based on explosion rate
    uint32_t interval = (uint32_t)(1000.0f / minefield.expl_per_sec);
    if (now - last_burst_ms >= interval) {
        last_burst_ms = now;
        spawn_explosion();
    }

    // advance, retire by lifetime, compute bounds & collect actives
    int active_indices[MAX_CONCURRENT_EXPLOSIONS], active_count = 0;
    for (int i = 0; i < MAX_CONCURRENT_EXPLOSIONS; ++i) {
        Explosion *xpl = &explosions[i];
        if (!xpl->active) continue;
        xpl->radius += xpl->speed * dt_s;
        if (xpl->radius > POLY_RADIUS + xpl->thickness) {
            xpl->active = false;
            continue;
        }
        float r0 = xpl->radius > xpl->thickness ? xpl->radius - xpl->thickness : 0.0f;
        xpl->min_r2 = r0 * r0;
        float r1 = xpl->radius + xpl->thickness;
        xpl->max_r2 = r1 * r1;
        active_indices[active_count++] = i;
    }

    // draw shells using per-instance thickness
    uint16_t total_pixels = mapping_get_total_pixels();
    for (uint16_t p = 0; p < total_pixels; ++p) {
        float best_w = 0.0f;
        uint8_t best_h = 0;
        Vec3 pos = led_pos[p];
        for (int ai = 0; ai < active_count; ++ai) {
            Explosion *xpl = &explosions[active_indices[ai]];
            Vec3 d = { pos.x - xpl->center.x,
                       pos.y - xpl->center.y,
                       pos.z - xpl->center.z };
            float dist2 = d.x*d.x + d.y*d.y + d.z*d.z;
            if (dist2 < xpl->min_r2 || dist2 > xpl->max_r2) continue;
            float dist = sqrtf(dist2);
            float delta = fabsf(dist - xpl->radius);
            if (delta > xpl->thickness) continue;
            float base = 1.0f - (delta / xpl->thickness);
            float radial = 1.0f - fminf(xpl->radius / (POLY_RADIUS + xpl->thickness), 1.0f);
            float w    = fast_powf(base, minefield.falloff_exp) * fast_powf(radial, minefield.radial_falloff_exp);
            if (w > best_w) {
                best_w = w;
                best_h = xpl->hue;
            }
        }
        if (best_w > 0.0f) {
            uint8_t intensity = (uint8_t)(best_w * 255);
            uint8_t r, g, b;
            hsv_to_rgb_rainbow(best_h,
                               255 - intensity / 2,
                               intensity,
                               &r, &g, &b);
            add_pixel_color(p, r, g, b);
        }
    }

    anim_time_end();
    update_leds();
}
