/*
 * font.h - Lisa 2 bitmap fonts (.LFT).
 *
 *   0x000  "LFT\0"
 *   0x004  u16  100, the version
 *   0x006  u16  number of glyphs
 *   0x008  u16  cell width
 *   0x00a  u16  height
 *   0x00c  224 x s32  offset of the glyph for characters 32..255 from
 *                     0x84c, or -1 when there is none
 *   0x38c  768  palette
 *   0x68c  224 x s16  glyph widths
 *   0x84c         glyph pixels, `width` x height each, rows top down
 *
 * Up to 30 fonts are loaded at once. Each glyph becomes a sprite, so
 * text draws through the colour table like any sprite.
 */

#ifndef IGN_LISA_FONT_H
#define IGN_LISA_FONT_H

#include "lisa/sprite.h"
#include "types.h"

enum {
    FONT_MAX = 30,
    FONT_GLYPHS = 224,
    FONT_ERR_NOT_READY = 0x3f2,     /* font_init: already done */
    FONT_ERR_NOT_INIT = 0x3fc,
    FONT_ERR_NO_SLOT = 0x406,
    FONT_ERR_BAD_FONT = 0x410,
    FONT_ERR_MAGIC = 0x41a,
    FONT_ERR_VERSION = 0x424,
    FONT_ERR_LOAD = 0x3e8
};

enum font_align {
    FONT_ALIGN_LEFT = 0,
    FONT_ALIGN_CENTER = 1,          /* x is the centre */
    FONT_ALIGN_RIGHT = 2            /* x is the right edge */
};

/* 0x640 bytes a font in the original, at 0x63f2e0. */
struct font {
    s32 used;                       /* +0x000 */
    s32 align;                      /* +0x004 */
    s32 disabled;                   /* +0x008, draws nothing */
    s32 proportional;               /* +0x00c */
    s32 spacing;                    /* +0x010, pixels between glyphs */
    u16 glyph_count;                /* +0x018 */
    u16 cell_width;                 /* +0x01a */
    u16 height;                     /* +0x01c */
    u8 present[FONT_GLYPHS];        /* +0x01e */
    struct sprite *glyphs[FONT_GLYPHS];     /* +0x100 */
    s16 widths[FONT_GLYPHS];        /* +0x480 */
};

extern struct font g_fonts[FONT_MAX];   /* 0x63f2e0 */

int font_init(void);

/* Loads a font file. Returns its handle, or -1 with g_file_error set. */
int font_load(const char *name, int unused);

/* Makes a font from a file image; the image can be freed after. */
int font_parse(const u8 *data, int unused);

/* Width of `text` in pixels; 2 for a disabled font. */
int text_width(const char *text, int font);

/* Draws `text` into the 2D target with its top at `y`. Returns 1, 2 for
 * a disabled font, FONT_ERR_BAD_FONT for an unknown one. */
int text_draw(const char *text, int font, int x, int y);

#endif /* IGN_LISA_FONT_H */
