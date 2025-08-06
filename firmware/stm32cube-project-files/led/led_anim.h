#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <stdint.h>       // for uint8_t, uint16_t
#include "led_render.h"   // for set_pixel_color(), update_leds(), etc.

// Live-editable index; set to 0xFF (i.e. UINT8_MAX) to disable per-vertex highlight.
extern uint8_t debug_highlight_vertex;

/**
 * @brief Convert an XYZ coordinate to a hue (0–255) based on polar angle in XY.
 */
void vertex_hue_from_xyz(const float v[3], uint8_t *out_hue, uint8_t hue_offset);

/**
 * @brief Convert HSV values to RGB (all 0–255).
 *        Fast integer math version.
 */

void anim_plasma_swirl_tick(void);

void anim_minefield_tick(void);

void anim_shooting_stars_tick(void);

void vertex_hsv_from_xyz(const float v[3],
                         uint8_t *out_hue,
                         uint8_t *out_sat,
                         uint8_t *out_val);

// 2‑Komponenten-Gradient: Hue und Value
void vertex_hv_from_xyz(const float v[3],
                        uint8_t *out_hue,
                        uint8_t *out_val);


void show_vertex_palette_xyz(uint8_t sat, uint8_t val, uint8_t hue_offset);

void show_vertex_gradient(uint8_t vertex, uint8_t sat, uint8_t val, uint8_t hue_offset);

/**
 * @brief Draw a per-vertex hue gradient (full palette), or only edges connected
 *        to the debug_highlight_vertex if it is set.
 * @param sat  Saturation (0–255)
 * @param val  Brightness (0–255)
 */
void show_vertex_palette(uint8_t sat, uint8_t val, uint8_t hue_offset);

/**
 * @brief Show a single face colored with a unique face-indexed RGB.
 * @param face_idx Index into polyhedron faces
 */
void show_face(uint8_t face_idx);

/**
 * @brief Animate continuous rainbow gradient across all LEDs.
 */
void anim_rainbow_tick(void);

/**
 * @brief Animate breathing white glow with sinusoidal brightness.
 */
void anim_breath_tick(void);

/**
 * @brief Animate random sparkles over faded background.
 */
void anim_twinkle_tick(void);

#endif // LED_ANIM_H
