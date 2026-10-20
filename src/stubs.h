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

/* Lisa library */
int lisa_tasks_init(void);          /* 0x45b1f0 */
int lisa_tasks_shutdown(void);      /* 0x45b240 */
void lisa_lib_shutdown(void);       /* 0x45b140 */
void render_init(void);             /* 0x45e610 */

/* game */
int game_video_init(void);          /* 0x417ea0 */
int game_cd_init(void);             /* 0x4184c0 */
int game_frame(void);               /* 0x4172b0 */
void game_exit(void);               /* 0x420b70 */

#endif /* IGN_STUBS_H */
