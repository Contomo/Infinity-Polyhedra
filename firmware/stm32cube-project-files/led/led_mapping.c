/* --------------------------------------------------------------------------
 * led_mapping.c – geometry-driven LED-to-edge mapping (renderer-agnostic)
 * -------------------------------------------------------------------------- */
#include "led_mapping.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "polyhedron.h"
#include "config.h"


#if defined(LED_DEBUG_MAPPING) || defined(LED_DEBUG_MAPPING_HEAP)
#include "usb_comms.h"   /* USBD_UsrLog */
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * DYNAMIC ARRAYS (allocated once per polyhedron)
 */
static uint8_t *leds_per_edge = NULL;   /* len = E */
static uint8_t *edge_map      = NULL;   /* len = E */
static bool    *flip_map      = NULL;   /* len = E */
static struct PixelMapping *pixel_map = NULL; /* len = total_pixels */

static EdgeLedInfo         *edge_info    = NULL;   /* len = E */

static uint16_t pixels_total = 0;       /* cached total LED count */
static uint8_t  edge_cnt     = 0;       /* cached p->E */

/* ─────────────────────────────────────────────────────────────────────────
 * PRIVATE FORWARD DECLARATIONS
 *
 */
static void  compute_leds_per_edge(const Polyhedron *p);
static bool  alloc_core_arrays(uint8_t E);
static void  free_core_arrays(void);
static size_t bytes_free_heap(void);

static void  build_edge_index_map(void);
static void mapping_build_pixel_map(void);
static void debug_print_mapping_heap(void);

/* ─────────────────────────────────────────────────────────────────────────
 * PUBLIC  API
 */
bool init_mapping(const Polyhedron           *p,
                  const uint8_t              *user_map,
                  const bool                 *user_flip,
                  uint8_t                     user_len)
{
    /* 0) tear down any previous buffers */
    free_core_arrays();


    /* 1) calculate expected sizes */
    edge_cnt = p->E;

    /* allocate leds_per_edge / edge_map / flip_map */
    if (!alloc_core_arrays(edge_cnt)) return false;

    /* 2) compute LED count per edge */
    compute_leds_per_edge(p);

    /* initialize remap / flip arrays */
    for (uint8_t i = 0; i < edge_cnt; ++i) {
        edge_map[i] = i;
        flip_map[i] = false;
    }
    if (user_map && user_flip && user_len == edge_cnt) {
        memcpy(edge_map, user_map,  edge_cnt);
        memcpy(flip_map, user_flip, edge_cnt);
    }

    /* 3) allocate pixel_map */
    size_t px_bytes = sizeof *pixel_map * pixels_total;
    pixel_map = malloc(px_bytes);
    if (!pixel_map) {
        free_core_arrays();
        return false;
    }

    update_mappings();
    debug_print_mapping_heap();
    return true;
}



void update_mappings(void){
    mapping_build_pixel_map();
    build_edge_index_map();
}


uint16_t 					 mapping_get_total_pixels(void)     { return pixels_total; }
const struct PixelMapping 	*mapping_get_map(void)      		{ return pixel_map;    }
const uint8_t 				*mapping_get_leds_per_edge(void)    { return leds_per_edge;}
uint8_t       				*mapping_edit_edge_map(void)        { return edge_map;     }
bool          				*mapping_edit_flip_map(void)        { return flip_map;     }

const EdgeLedInfo 			*mapping_get_edge_info(void) 		{return edge_info; }

/* ─────────────────────────────────────────────────────────────────────────
 * BUILD PIXEL_MAP (call after any remap change)
 */
static void mapping_build_pixel_map(void)
{
    if (!pixel_map || !leds_per_edge) return;

    uint16_t px_idx = 0;
    for (uint8_t logical = 0; logical < edge_cnt; ++logical) {
        uint8_t led_cnt = leds_per_edge[logical];
        uint8_t phys    = edge_map[logical];
        bool    rev     = flip_map[logical];

        uint16_t base = 0;
        for (uint8_t i = 0; i < phys; ++i)
            base += leds_per_edge[i];

        for (uint8_t i = 0; i < led_cnt; ++i) {
            uint16_t offset = rev ? (led_cnt - 1 - i) : i;
            pixel_map[px_idx].edge = logical;
            pixel_map[px_idx].phys = base + offset;
            ++px_idx;
        }
    }

}

static void build_edge_index_map(void)
{
    // edge_cnt, leds_per_edge[], edge_map[], flip_map[] are already initialized
    for (uint8_t e = 0; e < edge_cnt; ++e) {
        // which physical strip (block) this edge lives on
        uint8_t  phys_strip = edge_map[e];

        // compute base index = sum of LEDs in all earlier strips
        uint16_t base = 0;
        for (uint8_t s = 0; s < phys_strip; ++s) {
            base += leds_per_edge[s];
        }

        uint16_t cnt = leds_per_edge[e];       // number of LEDs on edge e
        bool     rev = flip_map[e];            // true → traverse B→A

        // pick the physical start index
        uint16_t start = rev
                       ? (uint16_t)(base + cnt - 1)  // last LED in this block
                       : (uint16_t)(base);           // first LED

        // signed step: +1 to go A→B, or -1 to go B→A
        int8_t step = rev ? -1 : +1;

        // write into edge_info[]
        edge_info[e].start = start;
        edge_info[e].count = cnt;
        edge_info[e].step  = step;
    }
}






/* ─────────────────────────────────────────────────────────────────────────
 * INTERNAL HELPERS
 */
static bool alloc_core_arrays(uint8_t E)
{
	leds_per_edge   = malloc(E * sizeof *leds_per_edge);
	edge_map        = malloc(E * sizeof *edge_map);
	flip_map        = malloc(E * sizeof *flip_map);

	edge_info  	= malloc(E * sizeof *edge_info);

    if (!leds_per_edge || !edge_map || !flip_map || !edge_info) {
        free_core_arrays();
        return false;
    }
    return true;
}
/* ─────────────────────────────────────────────────────────────────────────
 *
 */
static void free_core_arrays(void)
{
	free(leds_per_edge);   	leds_per_edge   = NULL;
	free(edge_map);        	edge_map        = NULL;
	free(flip_map);        	flip_map        = NULL;

	free(edge_info);  	edge_info  	= NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 *
 */
static void compute_leds_per_edge(const Polyhedron *p)
{
    /* longest edge */
    double max_len = 0.0;
    for (uint8_t e = 0; e < p->E; ++e) {
        const float *A = p->v[p->e[e].a];
        const float *B = p->v[p->e[e].b];
        double dx = A[0] - B[0], dy = A[1] - B[1], dz = A[2] - B[2];
        double len = sqrt(dx*dx + dy*dy + dz*dz);
        if (len > max_len) max_len = len;
    }

    pixels_total = 0;

#ifdef LED_DEBUG_MAPPING
    USBD_UsrLog("\n ");
    USBD_UsrLog("────[ compute_leds_per_edge ]────");
    USBD_UsrLog("=================================");
    USBD_UsrLog("   | edge   | length  | pixels |");
#endif
    for (uint8_t e = 0; e < p->E; ++e) {
        const float *A = p->v[p->e[e].a];
        const float *B = p->v[p->e[e].b];
        double dx = A[0] - B[0], dy = A[1] - B[1], dz = A[2] - B[2];
        double len = sqrt(dx*dx + dy*dy + dz*dz);
        double ratio = len / max_len;
        uint8_t leds = (uint8_t)roundf(ratio * (double)LEDS_LONGEST_EDGE);
        if (leds == 0) leds = 1;
        leds_per_edge[e] = leds;
        pixels_total += leds;

#ifdef LED_DEBUG_MAPPING
        USBD_UsrLog("   | %-6u | %-7.2f | %-6u |", (unsigned)e, len, (unsigned)leds);

#endif
    }
#ifdef LED_DEBUG_MAPPING
    USBD_UsrLog("\n ");
    USBD_UsrLog("   longest edge: length %-7.3f, pixels %-7u\n ", max_len, (unsigned)LEDS_LONGEST_EDGE);
#endif
}


/* ─────────────────────────────────────────────────────────────────────────
 *
 */
static void debug_print_mapping_heap(void)
{
#if defined(LED_DEBUG_MAPPING_HEAP)
    size_t core_bytes = edge_cnt * (
          sizeof *leds_per_edge
        + sizeof *edge_map
        + sizeof *flip_map
    );
    size_t edg_led_bytes = edge_cnt * (
          sizeof *edge_info
    );
    size_t px_bytes   = pixels_total * sizeof *pixel_map;
    size_t total_bytes= core_bytes + px_bytes + edg_led_bytes;

    USBD_UsrLog(
        "\n ───[ LED-Mapping-Heap ]───\n"
        "==========================\n"
        "   %-5u pixels\n"
        "   %-5u edges\n"
        "   %-5.1f kB core\n"
    	"   %-5.1f kB edge to led\n"
        "   %-5.1f kB pixel map\n"
        "   %-5.1f kB total\n"
        "   %-5.1f kB heap left\n "
        ,
        (unsigned)pixels_total,
        (unsigned)edge_cnt,
        core_bytes        / 1024.0f,
		edg_led_bytes	  / 1024.0f,
        px_bytes          / 1024.0f,
        total_bytes       / 1024.0f,
        bytes_free_heap() / 1024.0f
    );
}
/* ─────────────────────────────────────────────────────────────────────────
 * INTERNAL HELPERS
 */
extern char _estack;
extern char _sbrk(int incr);
static size_t bytes_free_heap(void) {
    char *brk = (char*)_sbrk(0);
    return (size_t) (&_estack - brk);
#endif
}

