/*
 * platform/common.h - host code shared by every host but the Vita's
 * (macOS, Linux, Windows, the web, Android, iOS): the DirectInput-style
 * key queue, scripted key presses, gamepad buttons as keys, and finding
 * the game data.
 */

#ifndef IGN_PLATFORM_COMMON_H
#define IGN_PLATFORM_COMMON_H

#include "types.h"

/* A key went down (1) or up (0): queues the event and updates
 * g_plat_keys. */
void plat_key_event(int dik, int down);

/* Presses and releases the keys plat_schedule_key queued, when their
 * time comes. Hosts call it from plat_pump_events. */
void plat_run_scheduled(void);

/* Gamepad buttons, whatever the pad calls them: A is the bottom face
 * button (Cross), B the right (Circle), X the left (Square), Y the top
 * (Triangle). The left stick counts as the d-pad. */
enum {
    PAD_LEFT = 1 << 0,
    PAD_RIGHT = 1 << 1,
    PAD_UP = 1 << 2,
    PAD_DOWN = 1 << 3,
    PAD_A = 1 << 4,
    PAD_B = 1 << 5,
    PAD_X = 1 << 6,
    PAD_Y = 1 << 7,
    PAD_L = 1 << 8,
    PAD_R = 1 << 9,
    PAD_START = 1 << 10,
    PAD_SELECT = 1 << 11
};

/* Presses and releases keys so that they follow the pad's buttons: in
 * the menus the d-pad and A / B, in a race the player's own keys. */
void plat_pad_update(u32 buttons);

/* The base folder the host keeps per-user files in, and the folder the
 * executable is in; each may be NULL. Implemented by each host. */
const char *plat_host_pref_dir(void);
const char *plat_host_exe_dir(void);

/* The game data folder when --data is not given: $IGNITION_DATA, then
 * `fallback` (the repository's data/ layout) when it exists, then a
 * disc/ folder, or a disc image to extract into one, beside the
 * executable or in the per-user folder. Extracts the image when the
 * folder does not have the game yet. Returns `fallback` when nothing is
 * found. */
const char *plat_find_data(const char *fallback);

#endif /* IGN_PLATFORM_COMMON_H */
