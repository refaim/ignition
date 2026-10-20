/*
 * app.c - the application shell around the game.
 */

#include "app.h"
#include "game.h"
#include "lisa/lisa.h"
#include "platform.h"
#include "stubs.h"
#include "video.h"

int g_app_state;                /* 0x493734 */
int g_app_active;               /* 0x4c5384 */
int g_app_fullscreen;           /* 0x493728 */
int g_app_ready;                /* 0x4c5390 */
u32 g_frame_time;               /* 0x49373c */

static int s_init_started;      /* 0x4937b0 */
static int s_done;              /* 0x4937b4 */
static int s_switching_mode;    /* 0x49372c */
static u32 s_time_last;         /* 0x4937b8 */
static u32 s_time_game;         /* 0x4937ac */

/* WinMain 0x4120a0, from RegisterClassA to the message loop: the high
 * resolution counter (timeBeginPeriod(1) without one; the platform
 * clock covers both), then the library and the devices. */
int app_startup(void)
{
    lisa_init();
    return app_init_devices();
}

/* 0x412500 */
int app_init_devices(void)
{
    video_select_driver(0);
    if (!video_create_window()) {
        return 0;
    }
    if (!input_init()) {
        return 0;
    }
    g_app_ready = 1;
    return 1;
}

/* 0x412460
 * The original reads QueryPerformanceCounter as milliseconds since
 * start up ((now - start) * 1000 / frequency, 64 bit) or timeGetTime,
 * and truncates to 32 bits. */
u32 app_time_ms(void)
{
    u32 now = plat_time_ms();
    u32 last = s_time_last != 0 ? s_time_last : now;

    if (g_app_active) {
        s_time_game += now - last;
    }
    s_time_last = now;
    return s_time_game;
}

/* 0x412230 */
int app_tick(void)
{
    if (g_app_state == APP_STATE_INIT && !s_init_started) {
        s_init_started = 1;
        g_frame_time = app_time_ms();
        game_init();
        g_app_state = APP_STATE_RUN;
        return 1;
    }
    if (g_app_state == APP_STATE_RUN && !s_done) {
        g_frame_time = app_time_ms();
        game_frame();
        return 1;
    }
    if (g_app_state == APP_STATE_SHUTDOWN && !s_done) {
        g_frame_time = app_time_ms();
        game_shutdown();
        lisa_shutdown();
        s_done = 1;
        return 0;
    }
    return 1;
}

/* 0x4122d0, WM_SYSKEYDOWN with VK_RETURN once the devices are up */
void app_toggle_fullscreen(void)
{
    if (!g_app_ready) {
        return;
    }
    s_switching_mode = 1;
    input_shutdown();
    video_shutdown();
    plat_destroy_window();
    g_app_fullscreen = !g_app_fullscreen;
    video_create_window();
    input_init();
    s_switching_mode = 0;
}

/* 0x4122d0, WM_DESTROY; 0x412550 */
void app_on_destroy(void)
{
    if (s_switching_mode) {
        return;
    }
    video_shutdown();
    input_shutdown();
    plat_request_quit();
}
