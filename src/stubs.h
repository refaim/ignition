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

#define STUB(addr, name) do { \
        static int s_seen; \
        if (!s_seen) { \
            s_seen = 1; \
            fprintf(stderr, "stub: %s (0x%x)\n", name, (unsigned)(addr)); \
        } \
    } while (0)

/* input: DirectInput */
int input_init(void);               /* 0x455ac0 */
void input_shutdown(void);          /* 0x455e20 */
void input_reset(void);             /* 0x455ab0 */

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

/* game */
int game_cd_init(void);             /* 0x4184c0 */
int game_frame(void);               /* 0x4172b0 */
void game_exit(void);               /* 0x420b70 */

#endif /* IGN_STUBS_H */
