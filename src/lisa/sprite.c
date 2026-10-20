/*
 * sprite.c - Lisa 2 sprites.
 */

#include <math.h>
#include <stddef.h>

#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "stubs.h"

enum { SPRITE_RECORDS = 2000 };

static struct sprite s_records[SPRITE_RECORDS];        /* 0x514bd8 */
static struct sprite *s_free[SPRITE_RECORDS];          /* 0x512c58 */
static int s_free_top;                                 /* 0x51a998 */

/* 0x45c7f0 */
void sprite_init(void)
{
    int i;

    for (i = 0; i < SPRITE_RECORDS; i++) {
        s_free[i] = &s_records[i];
        s_records[i].tex = 0;
    }
    s_free_top = SPRITE_RECORDS;
}

/* 0x45c830 */
static struct sprite *gfx_sprite_create(const struct tex_desc *desc, struct sprite *sprite)
{
    struct tex_desc copy;

    if (sprite == NULL) {
        if (desc == NULL || s_free_top == 0) {
            return NULL;
        }
        sprite = s_free[--s_free_top];
        sprite->tex = 0;
    } else {
        if (sprite->tex == 0) {
            return NULL;
        }
        if (desc == NULL) {
            obj_create(NULL, sprite->tex);
            sprite->tex = 0;
            s_free[s_free_top++] = sprite;
            return NULL;
        }
    }
    /* Only the size, flags and pixels go to the texture. */
    copy = (struct tex_desc){0};
    copy.width = desc->width;
    copy.height = desc->height;
    copy.flags = desc->flags;
    copy.pixels = desc->pixels;
    copy.stride = desc->stride;
    sprite->tex = obj_create(&copy, sprite->tex);
    sprite->hot_x = desc->hot_x;
    sprite->hot_y = desc->hot_y;
    return sprite;
}

/* 0x456d40 */
struct sprite *sprite_create(const struct tex_desc *desc, struct sprite *sprite)
{
    return gfx_sprite_create(desc, sprite);
}

/* 0x465bb5, the unrotated path: clip to the target, then every pixel
 * becomes table[src << 8 | dst]. The original walks each row from the
 * right; the pixels do not depend on each other. */
static void blit(const struct tex_desc *t, s32 x, s32 y)
{
    const struct gfx_target *g = &g_gfx_target;
    const u8 *table = g_gfx_color_table;
    const u8 *src = t->pixels;
    int w = t->width, h = t->height;
    u8 *dst;
    int i;

    if (src == NULL) {
        return;
    }
    if (y < g->clip_y0) {
        int skip = g->clip_y0 - y;

        if ((h -= skip) <= 0) {
            return;
        }
        src += skip * t->stride;
        y = g->clip_y0;
    }
    if (y + h - g->clip_y1 > 0 && (h -= y + h - g->clip_y1) <= 0) {
        return;
    }
    if (x < g->clip_x0) {
        int skip = g->clip_x0 - x;

        if ((w -= skip) <= 0) {
            return;
        }
        src += skip;
        x = g->clip_x0;
    }
    if (x + w - g->clip_x1 > 0 && (w -= x + w - g->clip_x1) <= 0) {
        return;
    }
    dst = g->base + y * g->pitch + x;
    while (h-- > 0) {
        for (i = 0; i < w; i++) {
            dst[i] = table[src[i] << 8 | dst[i]];
        }
        src += t->stride;
        dst += g->pitch;
    }
}

/* 0x457370 */
static void draw(const struct sprite *sprite, const struct sprite_pos *pos, const s32 *matrix)
{
    struct tex_desc t;

    tex_get_info(&t, sprite->tex);
    if (matrix != NULL) {
        STUB(0x468c70, "sprite_draw_rotated");
        return;
    }
    /* sar: the shift of a negative position rounds down. */
    blit(&t, (pos->x - sprite->hot_x) >> 8, (pos->y - sprite->hot_y) >> 8);
}

/* 0x4571b0
 * The matrix is { c, s, s, c } with c = cos(angle) * scale and
 * s = sin(angle) * scale in 16.16, as the original stores it. */
static int gfx_sprite_draw(const struct sprite *sprite, const struct sprite_pos *pos,
                           const struct sprite_xform *xform)
{
    s32 matrix[4];
    const s32 *m = NULL;

    if (xform != NULL) {
        s32 scale = (s32)(xform->scale * 65536.0f);

        matrix[0] = (s32)(cos(xform->angle) * scale);
        matrix[1] = (s32)(sin(xform->angle) * scale);
        matrix[2] = matrix[1];
        matrix[3] = matrix[0];
        m = matrix;
    }
    draw(sprite, pos, m);
    return 1;
}

/* 0x456d20 */
int sprite_draw(const struct sprite *sprite, const struct sprite_pos *pos,
                const struct sprite_xform *xform)
{
    return gfx_sprite_draw(sprite, pos, xform);
}
