/*
 * platform.h - the host: window, OpenGL context, message pump, keyboard,
 * clock.
 *
 * Replaces the Win32 parts of IGN_WIN.EXE (WinMain's message loop, the
 * window procedure, DirectDraw's window and DirectInput's keyboard). One
 * implementation per host: platform_macos.m, and platform_null.c, which
 * runs without a window for tests.
 *
 * Must not include a graphics API header; gfx.h must not include this.
 */

#ifndef IGN_PLATFORM_H
#define IGN_PLATFORM_H

#include "types.h"

/* Milliseconds since start up, wrapping at 32 bits. */
u32 plat_time_ms(void);

/* Opens the window with a current OpenGL 3.2 core context, or switches
 * an open one to the requested mode. `width` and `height` are the
 * windowed size in points. Returns 0 on failure. */
int plat_window_open(const char *title, int width, int height, int fullscreen);
void plat_destroy_window(void);

/* Drawable size in pixels (HiDPI aware). */
void plat_drawable_size(int *width, int *height);

/* Shows the frame drawn since the last call. */
void plat_present(void);

/* Keyboard state indexed by DirectInput scan code (DIK_*), 0x80 while a
 * key is down, the layout of IDirectInputDevice::GetDeviceState. */
extern u8 g_plat_keys[256];

/* Takes the oldest key event since the last call, as DirectInput's
 * buffered data gives it: scan code, and 1 for a press, 0 for a
 * release. Returns 0 when there is none. */
int plat_next_key_event(int *key, int *down);

/* Handles waiting window events without running a frame, so a loop
 * that polls input sees the keyboard, as DirectInput did. */
void plat_pump_events(void);

/* Queues a press and release of `key` `ms` after start up, as if typed;
 * for scripted test runs. */
void plat_schedule_key(u32 ms, int key, u32 hold);

/* Keeps the game running while the window is not active; the original
 * stops. For scripted runs. */
extern int g_plat_run_inactive;

/* Audio output, pulled: `cb` writes `frames` interleaved stereo floats
 * on the audio thread. Returns 0 when there is no output. */
typedef void (*plat_audio_fn)(float *out, s32 frames, void *user);
int plat_audio_open(s32 rate, plat_audio_fn cb, void *user);
void plat_audio_close(void);

/* Runs the message loop until the game quits: while the app is active,
 * app_tick runs whenever no message is waiting; while inactive the loop
 * waits for messages. Returns the exit code. */
int plat_run(void);

/* Like plat_run, but calls `frame` instead of app_tick, for tools that
 * show something without the game, and keeps running while inactive.
 * `frame` returns 0 to quit. */
int plat_run_with(int (*frame)(void *user), void *user);

void plat_request_quit(void);

#endif /* IGN_PLATFORM_H */
