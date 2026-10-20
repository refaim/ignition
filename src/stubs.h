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

#include "cd.h"
#include "input.h"
#include "sound.h"
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



/* sound */

int sound_music(int a, int b, int c);   /* 0x458d80 */
int joystick_read(void);            /* 0x459a20 */

/* front end pieces still to port */
int menu_network_page(void);        /* 0x40af54, inside menu_page_handler_a */
/* 3D, for the OpenGL renderer */
int menu_background_render(void);   /* 0x40c740 */
int car3d_draw(s32 car, int spin, double scale, int x, int y);   /* 0x40c5a0 */
int menucar_load(void);             /* 0x40ca60 */
int menucar_free(void);             /* 0x410920 */
int scene_free(void);               /* 0x446c30 */
int menu_apply_settings(void);      /* 0x404800 */
int net_send_settings(void *state, void *settings);    /* 0x40d200 */
int net_connect(void);              /* 0x459d40, the DirectPlay dialogs */
int net_start(void);                /* 0x459e80 */
int net_reset(void *state);         /* 0x40ce90 */
extern s32 g_net_players;           /* 0x50f7a8 */

/* 3D scene; the renderer's buffers are replaced by OpenGL */
void *scene_create(void);           /* 0x4468d0: returns the camera */
int raster_set_grid(int w, int h, int count);      /* 0x446ca0 */
int raster_set_screen(int w, int h, int flags);    /* 0x402be0 */

/* game */
int net_stop(void);                 /* 0x459e10, ends a network session */
void race_load_1(void);             /* 0x418b70 */
void race_load_2(void);             /* 0x418be0 */
void race_load_world(void);         /* 0x418c40 */
void race_frame(void);              /* 0x4173fb, inside game_frame */
void race_end(void);                /* 0x420870 */
void game_exit(void);               /* 0x420b70 */

#endif /* IGN_STUBS_H */
