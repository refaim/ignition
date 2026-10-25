/*
 * platform/platform.h - the host: window, OpenGL context, message pump, keyboard,
 * clock.
 *
 * Replaces the Win32 parts of IGN_WIN.EXE (WinMain's message loop, the
 * window procedure, DirectDraw's window and DirectInput's keyboard). One
 * implementation per host, on that system's own APIs: platform/macos.m,
 * platform/linux.c, platform/windows.c, platform/web.c,
 * platform/android.c, platform/ios.m and platform_vita.c. All but the
 * Vita's share platform_common.c.
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
/* Sizes the window's drawing area to width x height (points on macOS),
 * unless it is full screen; and gives the size it had. A host without
 * windows gives 0 x 0 and does nothing. */
void plat_window_size(int width, int height, int *old_width, int *old_height);
void plat_destroy_window(void);

/* Drawable size in pixels (HiDPI aware). */
void plat_drawable_size(int *width, int *height);

/* Shows the frame drawn since the last call. */
void plat_present(void);

/* Keyboard state indexed by DirectInput scan code (DIK_*), 0x80 while a
 * key is down, the layout of IDirectInputDevice::GetDeviceState. */
extern u8 g_plat_keys[256];

/* The first two gamepads as analog sticks, for the game's joystick
 * (input.c, joystick_read): the left stick's x and y from -100 (left,
 * up) to 100, and the accelerate and brake buttons (the right and left
 * triggers or A and B). A host with gamepads fills them as it reads its
 * pads; on one without, present stays 0. */
struct plat_stick {
    int present;
    int x, y;
    int buttons[2];
};

extern struct plat_stick g_plat_sticks[2];

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

/* Network play: one UDP socket over IPv4, for dplay.c, the stand-in for
 * the DirectPlay the original raced over. platform/udp.c has it on BSD
 * sockets and Winsock, platform/vita.c on sceNet; the web's host has no
 * UDP, and plat_net_open fails there. Addresses and ports are in host
 * byte order. */
struct plat_net_addr {
    u32 ip;
    u16 port;
};

#define PLAT_NET_ANY 0                  /* plat_net_open: any free port */
#define PLAT_NET_BROADCAST 0xffffffffu  /* 255.255.255.255 */
#define PLAT_NET_LOOPBACK 0x7f000001u   /* 127.0.0.1 */

/* Opens the socket, bound to `port` on every interface, not blocking,
 * with broadcasts allowed. A socket already open is closed first.
 * Returns 0 when the host has no network or the port is taken. */
int plat_net_open(u16 port);
void plat_net_close(void);
/* Sends one datagram. Returns 0 on failure. */
int plat_net_send(const struct plat_net_addr *to, const void *buf, int size);
/* Takes one waiting datagram: its size (cut to `size`), 0 when none is
 * waiting, -1 on an error. */
int plat_net_recv(struct plat_net_addr *from, void *buf, int size);
/* Waits up to `ms` milliseconds for a datagram to arrive. */
void plat_net_wait(u32 ms);
/* Looks up a host name or dotted address. Returns 0 when unknown. */
int plat_net_resolve(const char *name, u32 *ip);

#endif /* IGN_PLATFORM_H */
