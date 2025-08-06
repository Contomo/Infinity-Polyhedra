/* --------------------------------------------------------------------------
 * led_debug.c – interactive edge remapper
 * -------------------------------------------------------------------------- */
#include <stdlib.h>
#include <string.h>
#include "polyhedron.h"
#include "led_mapping.h"
#include "led_render.h"
#include "usb_comms.h"
#include "led_debug.h"
#include "led_anim.h"

extern Polyhedron poly;

/* ==========================================================================
 * Runtime state
 * ========================================================================== */
static DebugMode  dbg_mode        = DEBUG_MODE;
static uint8_t    dbg_face        = 0;
static uint8_t    dbg_edge_slot   = 0;
static uint16_t   dbg_bar_index   = 0;

uint8_t 	  debug_hue = 0;


static float      acc_bar   = 0.0f;
static float      acc_face  = 0.0f;
static float      acc_slot  = 0.0f;

static uint8_t   *saved_map = NULL;

static const uint32_t BLINK_MS = 300;

/* ==========================================================================
 *
 * ========================================================================== */

static void show_edge_reassignement(uint8_t face);

/* ==========================================================================
 *
 * ========================================================================== */


static inline void clear_saved(void) {if (saved_map) { free(saved_map); saved_map = NULL; } }

static inline void ensure_saved(void) { if (!saved_map) { saved_map = malloc(poly.E); memcpy(saved_map, mapping_edit_edge_map(), poly.E); } }

static inline void restore_saved(void) { if (saved_map) {memcpy(mapping_edit_edge_map(), saved_map, poly.E); } }


//MODE_FACE_EDIT         = 0,
//MODE_VERTEX_EDIT       = 1,
//MODE_SHOW_SORTED_EDGE  = 2,
//MODE_FACE_SHOW         = 99

 void debug_ui_tick(void)
 {
    if (dbg_mode == DEBUG_MODE)
    {
    	g_global_brightness = 255;
    	anim_minefield_tick();
    }
    else if (dbg_mode == ANIM_1)
    {
    	g_global_brightness = 255;
    	show_vertex_palette_xyz(255, 255, debug_hue);
    }
    else if (dbg_mode == ANIM_2)
    {
    	g_global_brightness = 255;
    	show_vertex_gradient(0, 255, 255, debug_hue);
    }
    else if (dbg_mode == ANIM_3)
    {
    	g_global_brightness = 255;
    	anim_shooting_stars_tick();
    }
    else if (dbg_mode == ANIM_4)
    {
    	g_global_brightness = 255;
    	anim_rainbow_tick();
    }
    else if (dbg_mode == ANIM_5)
    {
    	g_global_brightness = 255;
    	anim_plasma_swirl_tick();
    }
    else if (dbg_mode == ANIM_6)
    {
    	g_global_brightness = 40;
    	show_edge_reassignement(dbg_face);//
    }
 }


/* ==========================================================================
 *
 * ========================================================================== */
void debug_change_bar(float delta)
{
	if (dbg_mode != DEBUG_MODE) {
		return;
	}
    acc_bar += delta;
    int32_t steps = (int32_t)acc_bar;
    if (!steps) return;
    acc_bar -= steps;

    dbg_bar_index = (dbg_bar_index + steps + poly.E) % poly.E;

    ensure_saved();
    restore_saved();

    const uint8_t *verts = poly_face_vertices(&poly, dbg_face);
    uint8_t fv = poly_face_vertex_count(&poly, dbg_face);
    uint8_t v0 = verts[dbg_edge_slot];
    uint8_t v1 = verts[(dbg_edge_slot + 1) % fv];
    uint8_t logical_edge = poly_find_edge(&poly, v0, v1);

    uint8_t *emap = mapping_edit_edge_map();
    uint8_t tmp  = emap[logical_edge];
    emap[logical_edge]      = emap[dbg_bar_index];
    emap[dbg_bar_index]     = tmp;

    update_mappings();
    show_edge_reassignement(dbg_face);
}
/* ────────────────────────────────────────────────────────────────────────
 * change the active face were debugging
 */
void debug_change_face(float delta)
{
	if (dbg_mode != DEBUG_MODE) {
		return;
	}
    acc_face += delta;
    int32_t steps = (int32_t)acc_face;
    if (!steps) return;
    acc_face -= steps;

    dbg_face = (dbg_face + steps + poly.F) % poly.F;
    dbg_edge_slot = 0;
    clear_saved();
    show_edge_reassignement(dbg_face);
    static uint8_t last_face = 0xFF;
    if (dbg_face != last_face) {
        USBD_UsrLog("#face# %u", dbg_face);
        last_face = dbg_face;
    }

}
/* ────────────────────────────────────────────────────────────────────────
 *
 */
void debug_change_slot(float delta)
{
	if (dbg_mode != DEBUG_MODE) {
		return;
	}
    acc_slot += delta;
    int32_t steps = (int32_t)acc_slot;
    if (!steps) return;
    acc_slot -= steps;

    uint8_t fv = poly_face_vertex_count(&poly, dbg_face);
    dbg_edge_slot = (dbg_edge_slot + steps + fv) % fv;
    clear_saved();
    show_edge_reassignement(dbg_face);
}
/* ────────────────────────────────────────────────────────────────────────
 *
 */
void debug_toggle_flip(void)
{
	if (dbg_mode != DEBUG_MODE) {
		return;
	}
    ensure_saved();
    restore_saved();

    const uint8_t *verts = poly_face_vertices(&poly, dbg_face);
    uint8_t fv = poly_face_vertex_count(&poly, dbg_face);
    uint8_t v0 = verts[dbg_edge_slot];
    uint8_t v1 = verts[(dbg_edge_slot + 1) % fv];
    uint8_t e_id = poly_find_edge(&poly, v0, v1);

    bool *fmap = mapping_edit_flip_map();
    fmap[e_id] = !fmap[e_id];

    update_mappings();
    show_edge_reassignement(dbg_face);
}


/* ────────────────────────────────────────────────────────────────────────
 *
 */
static float debug_hue_acc = 0.0f;
void debug_change_hue(float delta)
{
    // 1) Akkumulieren
    debug_hue_acc += delta;

    // 2) Wrap-around im Float‑Bereich [0..255)
    debug_hue_acc = fmodf(debug_hue_acc, 255.0f);
    if (debug_hue_acc < 0.0f) {
        debug_hue_acc += 255.0f;
    }

    // 3) Int‑Hue setzen (0..254)
    debug_hue = (uint8_t)debug_hue_acc;
}




/* ==========================================================================
 *
 * ========================================================================== */
/* Global variable to track last blink update */
static uint32_t last_blink_time = 0;
static bool blink_on = false;

static void show_edge_reassignement(uint8_t face)
{
    set_all_pixels_color(0, 0, 0);

    // 1) prep
    const uint8_t         *verts  = poly_face_vertices(&poly, face);
    const uint8_t          fv     = poly_face_vertex_count(&poly, face);
    const PixelMapping    *pm     = mapping_get_map();            // length = total pixels
    const uint8_t         *lpe    = mapping_get_leds_per_edge(); // length = edge_cnt
    uint32_t               now    = ms();

    // blink toggle
    if ((now - last_blink_time) >= BLINK_MS) {
        blink_on = !blink_on;
        last_blink_time = now;
    }

    // 2) for each edge‐slot in this face
    for (uint8_t slot = 0; slot < fv; ++slot) {
        uint8_t v0   = verts[slot];
        uint8_t v1   = verts[(slot + 1) % fv];
        uint8_t edge = poly_find_edge(&poly, v0, v1);

        // 3) compute start and length of pixel_map block for this edge
        uint16_t start_idx = 0;
        for (uint8_t k = 0; k < edge; ++k) {
            start_idx += lpe[k];
        }
        uint16_t len  = lpe[edge];
        uint16_t half = len / 2;

        // 4) true face winding test
        bool ccw = poly_face_edge_is_ccw(&poly, face, edge);

        // 5) endpoint hues + blink sat
        uint8_t h0, h1;
        vertex_hue_from_xyz(poly.v[v0], &h0, debug_hue);
        vertex_hue_from_xyz(poly.v[v1], &h1, debug_hue);
        uint8_t sat = ((slot == dbg_edge_slot) && !blink_on) ? 128 : 255;

        // 6) draw first half in h0, second half in h1
        //    if ccw==false, reverse the block
        for (uint16_t i = 0; i < len; ++i) {
            uint16_t idx = ccw ? (start_idx + i)
                               : (start_idx + (len - 1 - i));

            uint16_t phys = pm[idx].phys;
            uint8_t  hue  = (i < half) ? h0 : h1;

            uint8_t r, g, b;
            hsv_to_rgb(hue, sat, 255, &r, &g, &b);
            set_pixel_color(phys, r, g, b);
        }
    }

    update_leds();
}


/* ────────────────────────────────────────────────────────────────────────
 *
 */


void debug_change_mode(uint8_t mode)
{
	dbg_mode = (DebugMode)mode;
}



 /* ==========================================================================
  * Dumps the current edge map and flip map as C initializers.
  * Uses the #noprefix# and #endnoprefix# tags to mark the dump section.
  * This ensures that the output can be directly copied without prefixes.
  * ========================================================================== */
 #define ENTRY_PER_LINE 8
 void debug_save_and_dump(void)
 {
     const uint8_t *emap = mapping_edit_edge_map();
     const bool    *fmap = mapping_edit_flip_map();

     // Start the no-prefix section for raw output
     USBD_UsrLog("#noprefix#\n ");

     // 1) Edge Map
     USBD_UsrLog("static const uint8_t USER_MAP[EDGE_CNT] = {");
     char line[128];
     for (uint8_t i = 0; i < poly.E; i += ENTRY_PER_LINE) {
         size_t off = 0;
         line[0] = '\0';

         // Add 4 spaces at the beginning of each line for indentation
         off += snprintf(line + off, sizeof(line) - off, "    ");

         for (uint8_t j = 0; j < ENTRY_PER_LINE && (i + j) < poly.E; ++j) {
             off += snprintf(line + off, sizeof(line) - off,
                             " %3u%s",
                             emap[i + j],
                             (i + j + 1 < poly.E) ? "," : "");
         }
         USBD_UsrLog("%s", line);
     }
     USBD_UsrLog("};\n ");

     // 2) Flip Map
     USBD_UsrLog("static const bool USER_FLIP[EDGE_CNT] = {");
     for (uint8_t i = 0; i < poly.E; i += ENTRY_PER_LINE / 2) {
         size_t off = 0;
         line[0] = '\0';

         // Add 4 spaces at the beginning of each line for indentation
         off += snprintf(line + off, sizeof(line) - off, "    ");

         for (uint8_t j = 0; j < ENTRY_PER_LINE / 2 && (i + j) < poly.E; ++j) {
             off += snprintf(line + off, sizeof(line) - off,
                             " %s%s",
                             fmap[i + j] ? "true" : "false",
                             (i + j + 1 < poly.E) ? "," : "");
         }
         USBD_UsrLog("%s", line);
     }
     USBD_UsrLog("};\n ");

     // End the no-prefix section
     USBD_UsrLog("#endnoprefix#");
 }
