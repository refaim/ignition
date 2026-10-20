/*
 * stubs.h - original routines not reimplemented yet.
 *
 * Each one prints its address to stderr the first time it runs, so a
 * missing piece is never silent. Move a routine out of here when it is
 * reimplemented.
 */

#ifndef IGN_STUBS_H
#define IGN_STUBS_H

#include <stdio.h>

#include "input.h"
#include "types.h"

#define STUB(addr, name) do { \
        static int s_seen; \
        if (!s_seen) { \
            s_seen = 1; \
            fprintf(stderr, "stub: %s (0x%x)\n", name, (unsigned)(addr)); \
        } \
    } while (0)

/* Lisa renderer set up, run by render_init (0x45e610) */
void render_init_45f490(void);      /* 0x45f490 */
void render_init_460740(void);      /* 0x460740 */
void render_init_4614d0(void);      /* 0x4614d0 */
void render_init_4607c0(void);      /* 0x4607c0 */
void render_init_45f640(void);      /* 0x45f640 */
void render_init_45fa50(void);      /* 0x45fa50 */
void render_init_460b50(void);      /* 0x460b50 */

/* Lisa rasteriser buffers, run by gfx_register_generic (0x456e60) */
void gfx_init_45d840(void);         /* 0x45d840 */
void gfx_init_spans(void);          /* 0x45c9f0 */

/* joystick and CD audio */
int joystick_init(void);            /* 0x4596a0 */
int cd_open(void);                  /* 0x458040 */
int cd_set_volume(int volume);      /* 0x457cf0 */

/* CD audio state, filled by cd_read_status and cd_read_tracks */
extern s32 g_cd_present;            /* 0x50ef4c */
extern s32 g_cd_tracks[4];          /* 0x50f2cc */
int cd_read_status(void);           /* 0x457b50 */
int cd_read_tracks(void);           /* 0x457da0 */

/* front end page handlers and tree callbacks; declared in menu.h:
 * menu_page_handler_a 0x409fb0, menu_page_handler_b 0x409610,
 * menu_cb_4045b0 0x4045b0, menu_cb_4045e0 0x4045e0,
 * menu_cb_404610 0x404610, menu_cb_404740 0x404740,
 * menu_cb_404790 0x404790 */
int menu_cb_4045b0(int arg0, int arg1);         /* 0x4045b0 */
int menu_cb_4045e0(int arg0, int arg1);         /* 0x4045e0 */
int menu_cb_404610(int arg0, int arg1);         /* 0x404610 */
int menu_cb_404740(int arg0, int arg1);         /* 0x404740 */
int menu_cb_404790(int arg0, int arg1);         /* 0x404790 */

/* sound */
void sound_tick(void);              /* 0x457890 */
int sound_open(s32 *params);        /* 0x4577a0: 0 off, 1 on, 2 no card */
int sound_load_bank(const char *dir, int flags);   /* 0x459140 */
int sound_set_volume(int volume);   /* 0x457ae0 */

void sound_stop_all(void);          /* 0x457910 */
void *sound_play(s32 sample);       /* 0x457aa0, returns the voice */
int sound_music(int a, int b, int c);   /* 0x458d80 */
s32 sound_load(int a, int b, s32 rate, s32 size);  /* 0x403d40 */
int cd_is_playing(void);            /* 0x457b10 */
int cd_play_track(int track);       /* 0x457ed0 */
extern s32 g_cd_track_count;        /* 0x50f070 */
int joystick_read(void);            /* 0x459a20 */

/* the intro animations (.CDP) */
int cdp_open(void *anim);           /* 0x412580 */
int cdp_next_frame(void *anim);     /* 0x412610 */
int cdp_draw(void *anim);           /* 0x412560 */

/* front end pieces still to port */
int menu_keys_page(void);           /* 0x40b820 */
int menu_best_times(void);          /* 0x40c1e0 */
int menu_network_page(void);        /* 0x40af54, inside menu_page_handler_a */
/* 3D, for the OpenGL renderer */
int menu_background_render(void);   /* 0x40c740 */
int car3d_draw(s32 car, int spin, double scale, int x, int y);   /* 0x40c5a0 */
int menucar_load(void);             /* 0x40ca60 */
int menu_apply_settings(void);      /* 0x404800 */
int net_send_settings(void *state, void *settings);    /* 0x40d200 */
extern s32 g_net_players;           /* 0x50f7a8 */

/* 3D scene; the renderer's buffers are replaced by OpenGL */
void *scene_create(void);           /* 0x4468d0: returns the camera */
int raster_set_grid(int w, int h, int count);      /* 0x446ca0 */
int raster_set_screen(int w, int h, int flags);    /* 0x402be0 */

/* game */
int cd_stop(void);                  /* 0x459e10 */
int menu_exit(void);                /* 0x4038f0 */
void race_load_1(void);             /* 0x418b70 */
void race_load_2(void);             /* 0x418be0 */
void race_load_world(void);         /* 0x418c40 */
void race_frame(void);              /* 0x4173fb, inside game_frame */
void race_end(void);                /* 0x420870 */
void game_exit(void);               /* 0x420b70 */

#endif /* IGN_STUBS_H */
