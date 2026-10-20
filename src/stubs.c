/*
 * stubs.c - original routines not reimplemented yet.
 */

#include "app.h"
#include "game.h"
#include "stubs.h"

int input_init(void)
{
    STUB(0x455ac0, "input_init");
    return 1;
}

void input_shutdown(void)
{
    STUB(0x455e20, "input_shutdown");
}

void input_reset(void)
{
    STUB(0x455ab0, "input_reset");
}






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

int game_cd_init(void)
{
    STUB(0x4184c0, "game_cd_init");
    return 1;
}

/* Until the game loop exists, ask for shut down after the first frame. */
int game_frame(void)
{
    STUB(0x4172b0, "game_frame");
    g_app_state = APP_STATE_SHUTDOWN;
    return 1;
}

void game_exit(void)
{
    STUB(0x420b70, "game_exit");
}
