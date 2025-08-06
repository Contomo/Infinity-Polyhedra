#ifndef LED_DEBUG_H
#define LED_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "led_mapping.h"  // For mapping helpers

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Debug UI modes (exposed for command interface and logic).
 */
typedef enum {
	DEBUG_MODE = 0,
	ANIM_1 = 1,
	ANIM_2 = 2,
	ANIM_3 = 3,
	ANIM_4 = 4,
	ANIM_5 = 5,
	ANIM_6 = 6,
} DebugMode;

extern uint8_t debug_hue;

/**
 * Re-render debug UI if enough time has elapsed.
 */
void debug_ui_tick(void);

/**
 * Change active face (cyclic, float delta accumulated).
 */
void debug_change_face(float delta);

/**
 * Change edge slot within active face (cyclic, float delta accumulated).
 */
void debug_change_slot(float delta);

/**
 * Change selected bar (physical bar index). Triggers edge reassignment.
 */
void debug_change_bar(float delta);

/**
 * Toggle flip direction for selected edge.
 */
void debug_toggle_flip(void);

/**
 * Save current edge and flip maps, then dump them to USB log.
 */
void debug_save_and_dump(void);

/**
 * Directly set debug mode by index.
 */
void debug_change_mode(uint8_t mode);

void debug_change_hue(float delta);

#ifdef __cplusplus
}
#endif

#endif // LED_DEBUG_H
