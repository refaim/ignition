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



s32 g_net_players;          /* 0x50f7a8 */

int net_send_settings(void *state, void *settings)
{
    (void)state;
    (void)settings;
    STUB(0x40d200, "net_send_settings");
    return 0;
}

/* No audio yet: the menu runs with sound off. */






