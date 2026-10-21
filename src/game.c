/*
 * game.c - the three phases app_tick drives, and the frame dispatcher.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "cd.h"
#include "dik.h"
#include "frontend.h"
#include "game.h"
#include "race_frame.h"
#include "input.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "lisa/sprite.h"
#include "startup.h"
#include "stubs.h"
#include "types.h"
#include "video.h"

int g_game_mode;                /* 0x639394, see enum game_mode */
int g_menu_active;              /* 0x563c3c */
int g_menu_leaving;             /* 0x563d9c */
int g_race_load_stage;          /* 0x553290, 1..3, one a frame */
int g_race_running;             /* 0x525e5c */
int g_race_ending;              /* 0x563bf4 */
int g_cd_ready;                 /* 0x5530dc */
int g_race_again;               /* 0x527f70 */
s32 g_race_mode;                /* 0x552f28 */
s32 g_back_from_race;           /* 0x54f9c8 */
s32 g_race_5285c8;              /* 0x5285c8 */
s32 g_race_4949b8;              /* 0x4949b8 */
s32 g_cd_last_track;            /* 0x4949e0 */
s32 g_net_state;                /* 0x50f9e4: 3 hosting, 4 joining */

/* The language, 0 to 4: English, German, Italian, then two that use
 * the English text here. The original keeps it at 0x639933, not word
 * aligned. */
int g_language;

enum game_mode {
    GAME_MODE_PLAY = 0,
    GAME_MODE_MENU = 1,         /* set up the front end */
    GAME_MODE_CHECK_CD = 2
};

/* 0x495990: two lines a language, 50 bytes each. */
static const char *const s_insert_cd[5][2] = {
    { "PLEASE INSERT THE IGNITION CD", "AND PRESS RETURN." },
    { "BITTE LEGEN SIE DIE BLEIFUSS FUN", "CD EIN UND DRhCKEN SIE RETURN." },
    { "INSERISCI IL CD DI IGNITION", "E PREMI INVIO." },
    { "PLEASE INSERT THE IGNITION CD", "AND PRESS RETURN." },
    { "PLEASE INSERT THE IGNITION CD", "AND PRESS RETURN." }
};

/* 0x417270 */
int game_init(void)
{
    g_game_mode = GAME_MODE_CHECK_CD;
    g_menu_active = 0;
    g_menu_leaving = 0;
    g_race_load_stage = 0;
    g_race_running = 0;
    g_race_ending = 0;
    g_fatal_error = 0;
    game_video_init();
    game_cd_init();
    return 1;
}

/* 0x4184c0
 * Reads the disc's track list, then reports the CD ready whatever it
 * found. The original copies three words from 0x50f2d4 on, one over the
 * other; the last stays. */
int game_cd_init(void)
{
    cd_read_status();
    if (g_cd_present == 1) {
        cd_read_tracks();
    }
    g_cd_last_track = g_cd_tracks[3];
    g_cd_ready = 1;
    g_game_mode = GAME_MODE_MENU;
    return 1;
}

/* 0x446410
 * Copies the back buffer to the screen surface and shows it. With
 * 0x4949c4 set the original blits to the front surface and does not
 * flip; both show the frame here. */
void present_back_buffer(void)
{
    video_blit(g_back_buffer, g_screen_width, 0, 0, g_screen_width, g_screen_height, 0, 0);
    video_flip();
}

/* 0x4182d0
 * "PLEASE INSERT THE IGNITION CD" until Return finds it (Escape quits),
 * then LOADING. */
void game_check_cd(void)
{
    int mini = g_fonts_menu[6];
    char text[52];

    g_gfx_color_table = g_identity_table;
    g_fonts[mini].align = FONT_ALIGN_CENTER;
    sprintf(text, "%s", s_insert_cd[g_language][0]);
    text_draw(text, mini, (int)(g_screen_width * 0.5), (int)(g_screen_height * 0.45));
    sprintf(text, "%s", s_insert_cd[g_language][1]);
    text_draw(text, mini, (int)(g_screen_width * 0.5), (int)(g_screen_height * 0.55));
    g_fonts[mini].align = FONT_ALIGN_LEFT;
    present_back_buffer();

    input_poll();
    if (input_key_down(DIK_RETURN) == 1) {
        game_cd_init();
    }
    if (input_key_down(DIK_ESCAPE) == 1) {
        g_app_state = APP_STATE_SHUTDOWN;
        g_game_mode = GAME_MODE_PLAY;
        g_cd_ready = 0;
    }
    if (g_cd_ready == 1) {
        memset(g_back_buffer, 0, (size_t)(g_screen_width * g_screen_height));
        g_fonts[mini].align = FONT_ALIGN_CENTER;
        sprintf(text, "LOADING");
        text_draw(text, mini, (int)(g_screen_width * 0.5), (int)(g_screen_height * 0.5));
        g_fonts[mini].align = FONT_ALIGN_LEFT;
        present_back_buffer();
    }
}

/* 0x4172b0, up to the race (0x4173fb) and from its end (0x417cad).
 *
 * The front end's result decides what follows: 2 starts a race, 3
 * quits. The original reads the result from an uninitialised local on
 * frames where the front end is not running but its exit (0x4038f0) is
 * still in progress; here the last result is kept. */
int game_frame(void)
{
    static int result;

    if (g_game_mode == GAME_MODE_CHECK_CD) {
        game_check_cd();
    }
    if (g_game_mode == GAME_MODE_MENU) {
        net_stop();
        g_net_state = 0;
        if (!menu_init()) {
            g_app_state = APP_STATE_SHUTDOWN;
            return 0;
        }
        g_game_mode = GAME_MODE_PLAY;
        g_menu_leaving = 0;
        g_menu_active = 1;
    }
    if (g_menu_active == 1) {
        result = menu_frame();
        if (result > 1) {
            g_menu_leaving = 1;
            g_game_mode = GAME_MODE_PLAY;
            g_menu_active = 0;
        }
    }
    if (g_menu_leaving == 1 && menu_exit()) {
        if (result == 2) {
            g_race_load_stage = 1;
            g_race_mode = 3;
            g_game_mode = GAME_MODE_PLAY;
            g_menu_active = 0;
            g_menu_leaving = 0;
            g_back_from_race = 0;
        } else if (result == 3) {
            g_app_state = APP_STATE_SHUTDOWN;
            return 0;
        }
    }
    if (g_race_load_stage == 3) {
        g_race_again = 0;
        race_load_world();
        g_race_5285c8 = 0;
        g_race_4949b8 = 1;
        g_race_load_stage = 0;
        g_race_running = 1;
        g_race_ending = 0;
    }
    if (g_race_load_stage == 2) {
        race_load_2();
        g_race_load_stage = 3;
        g_race_running = 0;
        g_race_ending = 0;
    }
    if (g_race_load_stage == 1) {
        race_load_1();
        g_race_load_stage = 2;
        g_race_running = 0;
        g_race_ending = 0;
    }
    if (g_race_running == 1) {
        race_frame();
    }
    if (g_race_ending == 1) {
        race_end();
        if (g_race_again == 1) {
            g_back_from_race = 0;
            g_race_running = 0;
            g_race_ending = 0;
            g_race_load_stage = 1;
            return 0;
        }
        g_back_from_race = 1;
        g_race_load_stage = 0;
        g_race_running = 0;
        g_race_ending = 0;
        g_game_mode = GAME_MODE_MENU;
    }
    return 0;
}

s32 g_fps_sum;                  /* 0x494988, the race's frame rates added up */
s32 g_fps_count;                /* 0x49498c */

/* 0x420b70 */
static void game_exit(void)
{
    int i;

    cd_stop_play();
    cd_close();
    free(g_menu_background);
    for (i = 0; i < 4; i++) {
        sprite_create(NULL, g_position_icons[i]);
    }
    sprite_create(NULL, g_bolt_icon);
    if (g_fps_count == 0) {
        g_fps_count = 1;
    }
    log_printf("Ignition has been shutdown normaly. Average framerate:%d\n",
               g_fps_sum / g_fps_count);
    log_close();
    mem_pool_destroy(g_game_pool);
}

/* 0x417e90 */
int game_shutdown(void)
{
    game_exit();
    return 0;
}
