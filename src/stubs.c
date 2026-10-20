/*
 * stubs.c - original routines not reimplemented yet.
 */

#include <stdlib.h>

#include "app.h"
#include "game.h"
#include "stubs.h"









#define STUB_VOID(addr, name) void name(void) { STUB(addr, #name); }

STUB_VOID(0x45f490, render_init_45f490)
STUB_VOID(0x460740, render_init_460740)
STUB_VOID(0x4614d0, render_init_4614d0)
STUB_VOID(0x4607c0, render_init_4607c0)
STUB_VOID(0x45f640, render_init_45f640)
STUB_VOID(0x45fa50, render_init_45fa50)
STUB_VOID(0x460b50, render_init_460b50)
STUB_VOID(0x45d840, gfx_init_45d840)
STUB_VOID(0x45c9f0, gfx_init_spans)

int joystick_init(void)
{
    STUB(0x4596a0, "joystick_init");
    return 1;
}

int cd_open(void)
{
    STUB(0x458040, "cd_open");
    return 1;
}

int cd_set_volume(int volume)
{
    (void)volume;
    STUB(0x457cf0, "cd_set_volume");
    return 1;
}



s32 g_cd_present;           /* 0x50ef4c */
s32 g_cd_tracks[4];         /* 0x50f2cc */

int cd_read_status(void)
{
    STUB(0x457b50, "cd_read_status");
    return 1;
}

int cd_read_tracks(void)
{
    STUB(0x457da0, "cd_read_tracks");
    return 1;
}

int cd_stop(void)
{
    STUB(0x459e10, "cd_stop");
    return 1;
}


/* Stays in the front end until it is ported. */
int menu_frame(void)
{
    STUB(0x402c00, "menu_frame");
    return 1;
}

int menu_exit(void)
{
    STUB(0x4038f0, "menu_exit");
    return 1;
}

STUB_VOID(0x457890, sound_tick)

/* No audio yet: the menu runs with sound off. */
int sound_open(s32 *params)
{
    (void)params;
    STUB(0x4577a0, "sound_open");
    return 0;
}

int sound_load_bank(const char *dir, int flags)
{
    (void)dir;
    (void)flags;
    STUB(0x459140, "sound_load_bank");
    return 1;
}

int sound_set_volume(int volume)
{
    (void)volume;
    STUB(0x457ae0, "sound_set_volume");
    return 1;
}

/* The camera record the menu writes to; the scene's object lists and
 * the rasteriser's tables are not made. */
void *scene_create(void)
{
    STUB(0x4468d0, "scene_create");
    return calloc(1, 0xa8);
}

int raster_set_grid(int w, int h, int count)
{
    (void)w;
    (void)h;
    (void)count;
    STUB(0x446ca0, "raster_set_grid");
    return 0;
}

int raster_set_screen(int w, int h, int flags)
{
    (void)w;
    (void)h;
    (void)flags;
    STUB(0x402be0, "raster_set_screen");
    return 1;
}

int menu_page_handler_a(void)
{
    STUB(0x409fb0, "menu_page_handler_a");
    return 0;
}

int menu_page_handler_b(int key, int action)
{
    (void)key;
    (void)action;
    STUB(0x409610, "menu_page_handler_b");
    return 0;
}

#define STUB_CB(addr, name) \
    int name(int arg0, int arg1) { (void)arg0; (void)arg1; STUB(addr, #name); return 0; }

STUB_CB(0x4045b0, menu_cb_4045b0)
STUB_CB(0x4045e0, menu_cb_4045e0)
STUB_CB(0x404610, menu_cb_404610)
STUB_CB(0x404740, menu_cb_404740)
STUB_CB(0x404790, menu_cb_404790)
STUB_VOID(0x418b70, race_load_1)
STUB_VOID(0x418be0, race_load_2)
STUB_VOID(0x418c40, race_load_world)
STUB_VOID(0x4173fb, race_frame)
STUB_VOID(0x420870, race_end)

void game_exit(void)
{
    STUB(0x420b70, "game_exit");
}
