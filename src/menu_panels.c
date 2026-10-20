/*
 * menu_panels.c - the front end's panels: the rounded boxes behind menu
 * text, built from tiles in MenuBkg.dat (0x4010f0..0x40183b).
 *
 * The screens add boxes to a list, then draw them all into a layer that
 * is blended over the picture. A box is one or more 19 line rows high;
 * where two boxes meet a row apart, their corners join with tiles that
 * bridge the gap.
 */

#include <stdlib.h>
#include <string.h>

#include "menu_panels.h"
#include "startup.h"

/* 0x18 bytes in the original. */
struct panel {
    s32 x0, x1;         /* +0x00, the inside, without the corners */
    s32 row;            /* +0x08, in 19 line rows */
    s32 y;              /* +0x0c, lines added */
    s32 highlight;      /* +0x10, colours through g_menu_remap */
    struct panel *next; /* +0x14 */
};

static struct panel *s_panels;          /* 0x47c030 */
static struct panel **s_tail = &s_panels;   /* 0x4bcd4c */
static u8 *s_dst;                       /* 0x4bcc44 */
static s32 s_pitch;                     /* 0x4bcc40 */
static s32 s_remap_fill;                /* 0x4bcd58 */
static s32 s_remap_top;                 /* 0x4bcd54 */
static s32 s_remap_bottom;              /* 0x4bcd48 */

/* 0x401120 */
static void panel_free(struct panel *p)
{
    if (p != NULL) {
        panel_free(p->next);
        free(p);
    }
}

/* 0x4010f0 */
void panels_clear(void)
{
    panel_free(s_panels);
    s_panels = NULL;
    s_tail = &s_panels;
}

/* 0x401140 */
void panel_add(int x, int row, int width, int y, int highlight)
{
    struct panel *p = calloc(1, sizeof(*p));

    p->x0 = x;
    p->row = row;
    p->x1 = x + width;
    p->y = y;
    p->highlight = highlight;
    p->next = NULL;
    *s_tail = p;
    s_tail = &p->next;
}

/* 0x4016d0 */
static void fill(u8 *dst, int count, u8 colour)
{
    if (count <= 0) {
        return;
    }
    memset(dst, s_remap_fill == 1 ? g_menu_remap[colour] : colour, (size_t)count);
}

static void tile_rows(const u8 **src, int w, u8 **dst, int rows, int remap)
{
    int x;

    while (rows-- > 0) {
        for (x = 0; x < w; x++) {
            u8 c = (*src)[x];

            if (c != 0) {
                (*dst)[x] = remap == 1 ? g_menu_remap[c] : c;
            }
        }
        *src -= w;
        *dst += s_pitch;
    }
}

/* 0x401730
 * A 19 line tile of `w` columns, stored bottom up; colour 0 is clear.
 * The top 10 lines and the bottom 9 take their own remap flag. */
static void tile(const u8 *src, int w, int h, u8 *dst)
{
    src += (h - 1) * w;
    tile_rows(&src, w, &dst, 10, s_remap_top);
    tile_rows(&src, w, &dst, 19 - 10, s_remap_bottom);
}

static u8 *at(const struct panel *p, int row_add, int x)
{
    return s_dst + ((p->row + row_add) * 19 + p->y) * s_pitch + x;
}

/* 0x4011c0 */
static void panel_draw(struct panel *n)
{
    const u8 *bkg = g_menu_background;
    struct panel *m;
    int top_left = 0, bottom_left = 0, top_right = 0, bottom_right = 0;
    int i;

    s_remap_fill = n->highlight;
    for (i = 0; i < 24; i++) {
        fill(s_dst + (n->row * 19 + n->y + i + 8) * s_pitch + n->x0 + 13,
             n->x1 - n->x0, bkg[0x9340 + i]);
    }
    for (m = s_panels; m != NULL; m = m->next) {
        int dx0, dx1;

        if (m == n) {
            continue;
        }
        dx0 = m->x0 - n->x0;
        dx1 = m->x1 - n->x1;
        if (n->row - m->row == 1) {
            /* m is the box above: bridge to its corners. */
            s_remap_top = m->highlight;
            s_remap_bottom = s_remap_fill;
            if (dx0 >= -13 && dx0 <= 13 && !top_left) {
                if (dx0 < 0) {
                    tile(bkg + 0x23a0 - 608 * dx0, 32, 19, at(n, 0, n->x0 - 19));
                } else {
                    tile(bkg + 0x4c0 + 608 * dx0, 32, 19, at(n, 0, n->x0 + dx0 - 19));
                }
                top_left = 1;
            }
            if (dx0 < -13 && n->x0 - m->x1 < -2) {
                tile(bkg + 0x8e80, 16, 19, at(n, 0, n->x0));
                top_left = 1;
            }
            if (dx0 > 13 && m->x0 - n->x1 < -2) {
                tile(bkg + 0x90e0, 16, 19, at(n, 0, n->x0 + dx0));
            }
            if (dx1 >= -15 && dx1 <= 15 && !top_right) {
                if (dx1 < 0) {
                    tile(bkg + 0x6880 - 608 * dx1, 32, 19, at(n, 0, n->x1 + dx1 + 13));
                } else {
                    tile(bkg + 0x44e0 + 608 * dx1, 32, 19, at(n, 0, n->x1 + 13));
                }
                top_right = 1;
            }
            if (dx1 > 15 && n->x1 - m->x0 > -1) {
                tile(bkg + 0x8fb0, 16, 19, at(n, 0, n->x1 + 13));
                top_right = 1;
            }
            if (dx1 < -15 && m->x1 - n->x0 > -1) {
                tile(bkg + 0x9210, 16, 19, at(n, 0, n->x1 + dx1 + 13));
            }
        }
        if (n->row - m->row == -1) {
            /* m is below: its top corners close this box. The original
             * tests the top right flag for the bottom right one. */
            if (dx0 >= -13 && dx0 <= 13 && !bottom_left) {
                bottom_left = 1;
            }
            if (dx1 >= -15 && dx1 <= 15 && !top_right) {
                bottom_right = 1;
            }
        }
    }
    s_remap_top = s_remap_fill;
    s_remap_bottom = s_remap_fill;
    if (!top_left) {
        tile(bkg, 16, 19, at(n, 0, n->x0 - 3));
    }
    if (!bottom_left) {
        tile(bkg + 0x260, 16, 19, at(n, 1, n->x0 - 3));
    }
    if (!top_right) {
        tile(bkg + 0x130, 16, 19, at(n, 0, n->x1 + 13));
    }
    if (!bottom_right) {
        tile(bkg + 0x390, 16, 19, at(n, 1, n->x1 + 13));
    }
}

/* 0x401190 */
void panels_draw(u8 *dst, int pitch)
{
    struct panel *p;

    s_dst = dst;
    s_pitch = pitch;
    for (p = s_panels; p != NULL; p = p->next) {
        panel_draw(p);
    }
}
