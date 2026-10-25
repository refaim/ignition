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


/* 3D scene; the renderer's buffers are replaced by OpenGL */

/* game */
void race_end(void);                /* 0x420870 */

#include "net.h"
#include "menu/bg.h"
#include "menu/car.h"
#include "scene.h"
#include "race/race.h"

#endif /* IGN_STUBS_H */
