/*
 * lisa/turned.h - the Lisa library's textured triangles for the 2D:
 * scaled and turned sprites (0x468c70 through 0x45ca50) and the
 * triangle lists of the gfx slot 0x50ebb8 (0x45d8a0).
 */

#ifndef IGN_LISA_TURNED_H
#define IGN_LISA_TURNED_H

#include "types.h"

/* A sprite as 0x457370 hands it to the rasteriser (0x4ba758): the hot
 * spot, the sprite's box in its 256 x 256 page and the page, all 8.8
 * texels, and the colour table its pixels go through. */
struct turned_src {
    s32 hot_x, hot_y;           /* +0x00 */
    s32 u0, v0, u1, v1;         /* +0x08 */
    const u8 *page;             /* +0x18, 64 KB: page[v << 8 | u] */
    const u8 *table;            /* +0x1c, [texel << 8 | dst] */
};

/* A textured triangle (0x4baf54): x, y in 8.8 pixels, u, v in 8.8
 * texels. */
struct turned_tri {
    s32 f00;                    /* +0x00 */
    s32 x0, y0, x1, y1, x2, y2; /* +0x04 */
    s32 u0, v0, u1, v1, u2, v2; /* +0x1c */
    const u8 *page;             /* +0x34 */
    const u8 *table;            /* +0x38 */
};

/* 0x45d840 and 0x45c9f0: the edge buffers. */
void gfx_init_45d840(void);
void gfx_init_spans(void);

/* 0x468c70: draws src scaled and turned by m (16.16, x = m0 u + m2 v,
 * y = m1 u + m3 v about the hot spot) at (x, y) in 8.8, into
 * g_gfx_target within its clip, as two triangles. */
void turned_sprite(const struct turned_src *src, const s32 *m, s32 x, s32 y);

/* 0x45ca50: a triangle through its table. */
void turned_triangle(const struct turned_tri *t);

/* 0x45d8a0: a triangle of plain texels. */
void turned_triangle_copy(const struct turned_tri *t);

#endif /* IGN_LISA_TURNED_H */
