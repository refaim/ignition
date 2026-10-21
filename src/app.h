/*
 * app.h - the application shell around the game: start up, the frame
 * loop's state machine, the game clock and shut down.
 *
 * In IGN_WIN.EXE this is WinMain (0x4120a0) and its window procedure
 * (0x4122d0). Here the platform layer owns the window and the message
 * pump, and calls into these.
 */

#ifndef IGN_APP_H
#define IGN_APP_H

#include "types.h"

enum app_state {
    APP_STATE_INIT = 0,
    APP_STATE_RUN = 1,
    APP_STATE_SHUTDOWN = 2
};

extern int g_app_state;         /* 0x493734 */
extern int g_app_active;        /* 0x4c5384, WM_ACTIVATEAPP */
extern int g_app_fullscreen;    /* 0x493728 */
extern int g_app_ready;         /* 0x4c5390, input and video are up */
extern u32 g_frame_time;        /* 0x49373c, game clock at frame start */

/* Before the window: the engine library and the clock. */
int app_startup(void);

/* Video and input. Returns 0 on failure. */
int app_init_devices(void);

/* One step of the state machine, run whenever no message is waiting.
 * Returns 0 once the game has shut down. */
int app_tick(void);

/* Milliseconds of game time. It runs only while the app is active. */
u32 app_time_ms(void);

/* Alt+Enter: rebuilds the video and input devices for the other mode. */
void app_toggle_fullscreen(void);
void app_toggle_renderer(void);

/* The window was destroyed (not by a mode switch). */
void app_on_destroy(void);

#endif /* IGN_APP_H */
