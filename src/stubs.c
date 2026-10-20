/*
 * stubs.c - original routines not reimplemented yet.
 */

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

int menu_init(void)
{
    STUB(0x4029a0, "menu_init");
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

STUB_VOID(0x418b70, race_load_1)
STUB_VOID(0x418be0, race_load_2)
STUB_VOID(0x418c40, race_load_world)
STUB_VOID(0x4173fb, race_frame)
STUB_VOID(0x420870, race_end)

void game_exit(void)
{
    STUB(0x420b70, "game_exit");
}
