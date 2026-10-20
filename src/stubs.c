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

int lisa_tasks_init(void)
{
    STUB(0x45b1f0, "lisa_tasks_init");
    return 1;
}

int lisa_tasks_shutdown(void)
{
    STUB(0x45b240, "lisa_tasks_shutdown");
    return 1;
}

void lisa_lib_shutdown(void)
{
    STUB(0x45b140, "lisa_lib_shutdown");
}

void render_init(void)
{
    STUB(0x45e610, "render_init");
}

int game_video_init(void)
{
    STUB(0x417ea0, "game_video_init");
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
