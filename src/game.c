/*
 * game.c - the three phases app_tick drives.
 */

#include "game.h"
#include "startup.h"
#include "stubs.h"
#include "types.h"

int g_game_mode;            /* 0x639394 */
static s32 s_reset_563c3c;  /* 0x563c3c */
static s32 s_reset_563d9c;  /* 0x563d9c */
static s32 s_reset_553290;  /* 0x553290 */
static s32 s_reset_525e5c;  /* 0x525e5c */
static s32 s_reset_563bf4;  /* 0x563bf4 */

/* 0x417270
 * Five of the six cleared globals are not identified yet. game_cd_init sets
 * g_game_mode to 1 at its end. */
int game_init(void)
{
    g_game_mode = 2;
    s_reset_563c3c = 0;
    s_reset_563d9c = 0;
    s_reset_553290 = 0;
    s_reset_525e5c = 0;
    s_reset_563bf4 = 0;
    g_fatal_error = 0;
    game_video_init();
    game_cd_init();
    return 1;
}

/* 0x417e90 */
int game_shutdown(void)
{
    game_exit();
    return 0;
}
