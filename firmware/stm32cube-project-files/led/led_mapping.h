#ifndef LED_MAPPING_H
#define LED_MAPPING_H

#include <stdint.h>
#include <stdbool.h>
#include "polyhedron.h"  // For Polyhedron and Edge definitions

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * User-override: number of LEDs on the longest edge (default = 24)
 * -------------------------------------------------------------------------- */
#ifndef LEDS_LONGEST_EDGE
#define LEDS_LONGEST_EDGE 24
#endif

/* --------------------------------------------------------------------------
 * PixelMapping
 * -------------------------------------------------------------------------- */
typedef struct PixelMapping {
    uint8_t  edge;    /* logical edge index */
    uint16_t phys;    /* physical LED start index */
} PixelMapping;

typedef struct {
    uint16_t 	start;  // physical index of the first LED on this edge
    uint16_t 	count;  // how many LEDs go on this edge
    int8_t  	step;    // +1 or –1 direction to walk the LEDs
} EdgeLedInfo;

/* --------------------------------------------------------------------------
 * Initialize and Shutdown Mapping
 * -------------------------------------------------------------------------- */

/**
 * Initialize LED-to-edge mapping for a given polyhedron.
 * Allocates internal arrays of length p->E and pixel_map of length total pixels.
 * If user_map and user_flip are non-NULL and user_len == p->E, copies them;
 * otherwise uses identity mapping and no flips.
 * Returns true on success, false on memory allocation failure.
 */
bool init_mapping(const Polyhedron *p,
                  const uint8_t    *user_map,
                  const bool       *user_flip,
                  uint8_t           user_len);

/**
 * Shutdown mapping and free all allocated memory.
 */
void mapping_shutdown(void);

/* --------------------------------------------------------------------------
 * Mapping Accessors
 * -------------------------------------------------------------------------- */

/**
 * Get total number of LEDs across all edges.
 */
uint16_t mapping_get_total_pixels(void);

/**
 * Get pointer to pixel_map array (length = mapping_get_total_pixels()).
 */
const PixelMapping *mapping_get_map(void);

/**
 * Get pointer to array of LEDs per edge (length = p->E).
 */
const uint8_t *mapping_get_leds_per_edge(void);

/**
 * Get pointer to edge_map[] for in-place editing (length = p->E).
 */
uint8_t *mapping_edit_edge_map(void);

/**
 * Get pointer to flip_map[] for in-place editing (length = p->E).
 */
bool *mapping_edit_flip_map(void);


/** Returns a pointer to an array[poly.E] of EdgeLedInfo */
const EdgeLedInfo *mapping_get_edge_info(void);

/* --------------------------------------------------------------------------
 * Build Pixel Map
 * Rebuilds the pixel_map array after modifying edge_map or flip_map.
 */
void update_mappings(void);


#ifdef __cplusplus
}
#endif

#endif // LED_MAPPING_H
