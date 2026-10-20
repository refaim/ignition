/*
 * gfx.c - Lisa 2 2D draw target.
 */

#include "lisa/gfx.h"

struct gfx_target g_gfx_target;
u8 *g_gfx_color_table;      /* 0x63f2d8 */

/* 0x456f60 */
static int gfx_set_target(u8 *base, int pitch, int width, int height, int bpp)
{
    struct gfx_target *t = &g_gfx_target;

    t->base = base;
    t->pitch = pitch;
    t->width = width;
    t->height = height;
    t->bpp = bpp;
    t->clip_x0 = 0;
    t->clip_y0 = 0;
    t->clip_x1 = width;
    t->clip_y1 = height;
    return 1;
}

/* 0x456cc0 */
int set_target(u8 *base, int pitch, int width, int height, int bpp)
{
    return gfx_set_target(base, pitch, width, height, bpp);
}

/* 0x457020 */
int gfx_set_clip(int x0, int y0, int x1, int y1)
{
    struct gfx_target *t = &g_gfx_target;

    if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0
        || x1 > t->width || y1 > t->height) {
        return 0;
    }
    t->clip_x0 = x0;
    t->clip_y0 = y0;
    t->clip_x1 = x1;
    t->clip_y1 = y1;
    return 1;
}
