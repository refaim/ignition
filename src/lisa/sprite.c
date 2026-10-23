/*
 * sprite.c - Lisa 2 sprites.
 */

#include <math.h>
#include <stddef.h>

#include "lisa/gfx.h"
#include "canvas.h"
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

/* 0x468c70: a scaled or turned sprite. The original sends the sprite's
 * quad, its corners turned by m about the hot spot, as two textured
 * triangles to the rasteriser (0x45ca50); here each pixel of the quad's
 * box inside the clip is mapped back into the sprite, and those that
 * land in it become table[src << 8 | dst] as the unturned path's. x, y
 * are the top left in 8.8 before turning, hx, hy the hot spot. */
static void blit_turned(const struct tex_desc *t, s32 x, s32 y, s32 hx, s32 hy, const s32 *m)
{
    const struct gfx_target *g = &g_gfx_target;
    const u8 *table = g_gfx_color_table;
    double a = m[0] / 65536.0, b = m[1] / 65536.0, c = m[2] / 65536.0, d = m[3] / 65536.0;
    double cx = (x + hx) / 256.0, cy = (y + hy) / 256.0, hxf = hx / 256.0, hyf = hy / 256.0;
    double det = a * d + b * c, lo_x = 1e9, lo_y = 1e9, hi_x = -1e9, hi_y = -1e9;
    int k, px, py, x0, y0, x1, y1;

    if (t->pixels == NULL || det == 0.0) {
        return;
    }
    for (k = 0; k < 4; k++) {
        double tx = (k & 1) * t->width - hxf, ty = (k >> 1) * t->height - hyf;
        double qx = cx + a * tx - b * ty, qy = cy + c * tx + d * ty;

        lo_x = qx < lo_x ? qx : lo_x;
        hi_x = qx > hi_x ? qx : hi_x;
        lo_y = qy < lo_y ? qy : lo_y;
        hi_y = qy > hi_y ? qy : hi_y;
    }
    x0 = (int)floor(lo_x) < g->clip_x0 ? g->clip_x0 : (int)floor(lo_x);
    y0 = (int)floor(lo_y) < g->clip_y0 ? g->clip_y0 : (int)floor(lo_y);
    x1 = (int)ceil(hi_x) > g->clip_x1 ? g->clip_x1 : (int)ceil(hi_x);
    y1 = (int)ceil(hi_y) > g->clip_y1 ? g->clip_y1 : (int)ceil(hi_y);
    for (py = y0; py < y1; py++) {
        u8 *dst = g->base + py * g->pitch;

        for (px = x0; px < x1; px++) {
            /* The pixel's middle, back through m: (dx, dy) = M (tx, ty)
             * with M = [a -b; c d]. */
            double dx = px + 0.5 - cx, dy = py + 0.5 - cy;
            double tx = (d * dx + b * dy) / det + hxf, ty = (a * dy - c * dx) / det + hyf;
            int u = (int)floor(tx), v = (int)floor(ty);

            if (u >= 0 && v >= 0 && u < t->width && v < t->height) {
                dst[px] = table[t->pixels[v * t->stride + u] << 8 | dst[px]];
            }
        }
    }
}

/* 0x457370 */
static void draw(const struct sprite *sprite, const struct sprite_pos *pos, const s32 *matrix)
{
    struct tex_desc t;

    tex_get_info(&t, sprite->tex);
    if (canvas_sprite(t.pixels, t.width, t.height, t.stride,
                      matrix != NULL ? pos->x - sprite->hot_x
                                     : ((pos->x - sprite->hot_x) >> 8) * 256,
                      matrix != NULL ? pos->y - sprite->hot_y
                                     : ((pos->y - sprite->hot_y) >> 8) * 256,
                      sprite->hot_x, sprite->hot_y, matrix)) {
        return;
    }
    if (matrix != NULL) {
        blit_turned(&t, pos->x - sprite->hot_x, pos->y - sprite->hot_y, sprite->hot_x,
                    sprite->hot_y, matrix);
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
