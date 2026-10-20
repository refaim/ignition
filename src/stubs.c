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








int net_stop(void)
{
    STUB(0x459e10, "net_stop");
    return 1;
}







int sound_music(int a, int b, int c)
{
    (void)a;
    (void)b;
    (void)c;
    STUB(0x458d80, "sound_music");
    return 0;
}





int joystick_read(void)
{
    STUB(0x459a20, "joystick_read");
    return 0;
}




int menu_network_page(void)
{
    STUB(0x40af54, "menu_network_page");
    return 0;
}

int menu_background_render(void)
{
    STUB(0x40c740, "menu_background_render");
    return 0;
}

int car3d_draw(s32 car, int spin, double scale, int x, int y)
{
    (void)car;
    (void)spin;
    (void)scale;
    (void)x;
    (void)y;
    STUB(0x40c5a0, "car3d_draw");
    return 0;
}

int net_connect(void)
{
    STUB(0x459d40, "net_connect");
    return 0;
}

int net_start(void)
{
    STUB(0x459e80, "net_start");
    return 0;
}

int net_reset(void *state)
{
    (void)state;
    STUB(0x40ce90, "net_reset");
    return 0;
}

int menucar_free(void)
{
    STUB(0x410920, "menucar_free");
    return 1;
}

int scene_free(void)
{
    STUB(0x446c30, "scene_free");
    return 1;
}



int menucar_load(void)
{
    STUB(0x40ca60, "menucar_load");
    return 1;
}

int menu_apply_settings(void)
{
    STUB(0x404800, "menu_apply_settings");
    return 1;
}

s32 g_net_players;          /* 0x50f7a8 */

int net_send_settings(void *state, void *settings)
{
    (void)state;
    (void)settings;
    STUB(0x40d200, "net_send_settings");
    return 0;
}

/* No audio yet: the menu runs with sound off. */



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



STUB_VOID(0x418b70, race_load_1)
STUB_VOID(0x418be0, race_load_2)
STUB_VOID(0x418c40, race_load_world)
STUB_VOID(0x4173fb, race_frame)
STUB_VOID(0x420870, race_end)

void game_exit(void)
{
    STUB(0x420b70, "game_exit");
}
