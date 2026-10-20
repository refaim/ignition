/*
 * video.c - the Lisa video layer, on OpenGL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"
#include "platform.h"
#include "video.h"
#include "lisa/sprite.h"
#include "stubs.h"

struct video_mode g_video_mode;     /* 0x4ba6e0 */
u8 *g_video_screen;
u8 g_video_palette[768];

static int s_gfx_ready;
static int s_screen_w, s_screen_h;

static int gfx_register_generic(void);

/* 0x456af0
 * Driver 0 fills the generic table at 0x50eba0 (0x456e60) and the
 * DirectDraw table at 0x50eb68 (0x45b690). This file stands in for both;
 * there is nothing to fill. */
int video_select_driver(int driver)
{
    if (driver != 0) {
        return 2;
    }
    gfx_register_generic();
    return 1;
}

/* 0x456e60
 * Fills the generic table; what is left to do is set up the
 * rasteriser's buffers and the sprite records. */
static int gfx_register_generic(void)
{
    gfx_init_45d840();
    gfx_init_spans();
    sprite_init();
    return 1;
}

/* 0x456bc0, through the table entries 0x50eb6c and 0x50eba0.
 * The window opens wide; the original's full screen mode is a full
 * screen window. */
int video_create_window(void)
{
    if (!plat_window_open("Ignition", 1280, 720, g_app_fullscreen)) {
        return 0;
    }
    if (!s_gfx_ready) {
        if (!gfx_init()) {
            return 0;
        }
        s_gfx_ready = 1;
    }
    return 1;
}

/* 0x456bf0. The GL context outlives mode switches; see
 * plat_destroy_window. */
void video_shutdown(void)
{
}

/* 0x456be0, through 0x50eb70 */
int video_set_mode(void)
{
    u8 *screen;

    if (g_video_mode.bpp != 8 || g_video_mode.width <= 0 || g_video_mode.height <= 0) {
        return 0;
    }
    screen = calloc((size_t)g_video_mode.width, (size_t)g_video_mode.height);
    if (screen == NULL) {
        return 0;
    }
    free(g_video_screen);
    g_video_screen = screen;
    s_screen_w = g_video_mode.width;
    s_screen_h = g_video_mode.height;
    return 1;
}

float video_aspect(void)
{
    if (s_screen_w == 320 && s_screen_h == 200) {
        return 4.0f / 3.0f;
    }
    return (float)s_screen_w / (float)s_screen_h;
}

void video_present(void)
{
    int w, h;

    if (!s_gfx_ready || g_video_screen == NULL) {
        return;
    }
    plat_drawable_size(&w, &h);
    gfx_begin_frame(w, h);
    gfx_draw_screen(g_video_screen, s_screen_w, s_screen_h, s_screen_w,
                    g_video_palette, video_aspect());
    plat_present();
}
