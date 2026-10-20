/*
 * gfx.h - Lisa 2 2D draw target: the 8 bit buffer that text and sprites
 * are drawn into, its clip rectangle, and the colour table that combines
 * a source pixel with the pixel under it.
 *
 * The original keeps the target twice, for the generic driver
 * (0x4ba6f8, 0x50ebf0..0x50ed38) and for the rasteriser (0x63f2b0..
 * 0x63f2d4), with the clip also in 24.8 fixed point. One copy serves
 * here.
 */

#ifndef IGN_LISA_GFX_H
#define IGN_LISA_GFX_H

#include "types.h"

struct gfx_target {
    u8 *base;       /* 0x4ba6f8, 0x63f2d4 */
    s32 pitch;      /* 0x50ebf4, 0x63f2c8 */
    s32 width;      /* 0x50ed2c */
    s32 height;     /* 0x50ed38 */
    s32 bpp;        /* 0x50ed34 */
    s32 clip_x0;    /* 0x50ed20, 0x63f2c0 */
    s32 clip_y0;    /* 0x50ed24, 0x63f2b8 */
    s32 clip_x1;    /* 0x50ed28, 0x63f2bc, exclusive */
    s32 clip_y1;    /* 0x50ed30, 0x63f2b4, exclusive */
};

extern struct gfx_target g_gfx_target;

/* 256 x 256 bytes: the result of drawing source colour s over
 * destination colour d is table[s << 8 | d]. */
extern u8 *g_gfx_color_table;   /* 0x63f2d8 */

/* 0x456cc0, through the table entry 0x50ebb0. Sets the target and a
 * clip rectangle covering all of it. Returns 1. */
int set_target(u8 *base, int pitch, int width, int height, int bpp);

/* 0x457020, through 0x50ebb4. Returns 0, leaving the clip alone, when
 * the rectangle is empty or not inside the target. */
int gfx_set_clip(int x0, int y0, int x1, int y1);

#endif /* IGN_LISA_GFX_H */
