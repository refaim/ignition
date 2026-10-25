/*
 * soft3d/raster.c - the rasteriser's screen and its dispatch (0x44f070,
 * 0x44f0e9), and the line through a table that 0x44f0e9 draws itself.
 */

#include <string.h>

#include "soft3d/soft3d.h"
#include "soft3d/sw.h"

struct sw_raster g_sw_raster;           /* 0x498730 */

u8 *g_sw_dst;                           /* 0x49c9f0 */
const u8 *g_sw_point_table;             /* 0x49c9f8 */
s32 g_sw_pitch;                         /* 0x49ca3c */
s32 g_sw_height;                        /* 0x49ca40 */
s32 g_sw_rows[600];                     /* 0x49ca44 */
u32 g_sw_recip[15002];                  /* 0x49d3a8 */
s32 r_49c9fc, r_49ca00, r_49ca04, r_49ca08;
s32 r_49ca0c, r_49ca10, r_49ca14, r_49ca18;
s32 r_49ca1c, r_49ca20, r_49ca24, r_49ca28;

static const struct sw_raster *s_ctx;  /* 0x49c9e4 */
static s32 s_step_down, s_step_up;      /* 0x4afd80, 0x4afd84: pitch + 1, 1 - pitch */

/* 0x44f1e4: the line's diagonal steps for a screen `w` wide. */
static void hook_44f1e4(int w, int h)
{
    (void)h;
    s_step_down = w + 1;
    s_step_up = 1 - w;
}

/* 0x44f070
 * The screen the rasteriser draws into, w x h with a pitch of w; at most
 * 1400 x 600. Fills the reciprocal table and the row offsets, and runs
 * each filler's hook. Returns -1 for a screen too big. */
int sw_raster_set_screen(int w, int h)
{
    static void (*const hooks[])(int, int) = {     /* 0x4abe6c */
        sw_hook_451730, sw_hook_45498c, sw_hook_450684, sw_hook_452ebc,
        sw_hook_44ff20, sw_hook_452770, sw_hook_452010, hook_44f1e4, sw_hook_453c33
    };
    u32 n;
    int i;

    if (w > 0x578 || h > 0x258) {
        return -1;
    }
    g_sw_pitch = w;
    g_sw_height = h;
    for (n = 1; n < 0x3a9b; n++) {
        g_sw_recip[n - 1] = 0x10000u / n - 1;
    }
    for (i = 0; i < (int)(sizeof(hooks) / sizeof(hooks[0])); i++) {
        hooks[i](w, h);
    }
    for (i = 0; i < 600; i++) {
        g_sw_rows[i] = i * w;
    }
    return 0;
}

/* Case 13 of 0x44f0e9: a line whose colour runs from c0 to c1 (8.8),
 * each pixel table[(c >> 8) << 8 | dst]. The line is clipped to the
 * screen by cutting its ends, the colour with it. */
static s32 s_lx0, s_ly0, s_lx1, s_ly1;  /* 0x4abe94..0x4abea0 */
static s32 s_lt;                        /* 0x4abea4 */
static const u8 *s_ltable;              /* 0x4abea8 */
static s32 s_ldc;                       /* 0x4abeac */
static s32 s_lc;                        /* 0x4afd90 */

/* The share of a run of `n` pixels that `k` of them make, in 1.31:
 * (k << 32) / n >> 1, as div does it. */
static s32 share(s32 k, s32 n)
{
    return (s32)((((u64)(u32)k << 32) / (u32)n) >> 1);
}

static void plot(u8 *p, u16 c)
{
    *p = s_ltable[(c & 0xff00) | *p];
}

static void line_draw(s32 x0, s32 y0, s32 x1, s32 y1)
{
    u8 *p = g_sw_dst + g_sw_rows[y0] + x0;
    s32 cols = x1 - x0 + 1, dy = y1 - y0, rows, step;
    s32 pitch = dy < 0 ? -g_sw_pitch : g_sw_pitch;
    u16 frac, acc = 0x7fff, c = (u16)s_lc, dc, count;

    rows = dy < 0 ? -dy + 1 : dy + 1;
    if (cols <= rows) {
        /* Steep: a column at a time, `step` rows and one more when the
         * fraction carries. */
        dc = (u16)sw_div(s_ldc, rows);
        step = (u16)rows / (u16)cols;
        frac = (u16)((((u32)((u16)rows % (u16)cols)) << 16) / (u16)cols);
        for (count = (u16)cols; count != 0; count--) {
            u32 sum = (u32)acc + frac;
            int k;

            acc = (u16)sum;
            if (sum > 0xffff) {
                plot(p, c);
                c = (u16)(c + dc);
                p += pitch;
            }
            for (k = (u16)step; k != 0; k--) {
                plot(p, c);
                c = (u16)(c + dc);
                p += pitch;
            }
            p++;
        }
    } else {
        dc = (u16)sw_div(s_ldc, cols);
        step = (u16)cols / (u16)rows;
        frac = (u16)((((u32)((u16)cols % (u16)rows)) << 16) / (u16)rows);
        for (count = (u16)rows; count != 0; count--) {
            u32 sum = (u32)acc + frac;
            int k;

            acc = (u16)sum;
            if (sum > 0xffff) {
                plot(p, c);
                c = (u16)(c + dc);
                p++;
            }
            for (k = (u16)step; k != 0; k--) {
                plot(p, c);
                c = (u16)(c + dc);
                p++;
            }
            p += pitch;
        }
    }
}

static void line_shade(const struct sw_cmd_line_shade *l)
{
    s32 x0 = l->x0 >> 8, x1 = l->x1 >> 8, y0, y1, c0 = l->c0, c1 = l->c1, t;

    if (x0 > x1) {
        t = c0;
        c0 = c1;
        c1 = t;
    }
    s_ldc = c1 - c0;
    s_lc = c0;
    s_ltable = l->table;
    y0 = l->y0 >> 8;
    y1 = l->y1 >> 8;
    if (x0 > x1) {
        t = x0, x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
    }
    /* Left of the clip: cut the left end. */
    if (x0 < r_49c9fc) {
        if (x1 < r_49c9fc) {
            return;
        }
        s_lt = share(x1 - r_49c9fc + 1, x1 - x0 + 1);
        t = y1 - y0;
        y0 += t - sw_mulhi(t * 2, s_lt);
        x0 = r_49c9fc;
        t = s_lc + s_ldc;
        s_ldc = sw_mulhi(s_ldc * 2, s_lt);
        s_lc = t - s_ldc;
    }
    if (x1 > r_49ca04) {
        if (x0 > r_49ca04) {
            return;
        }
        s_lt = share(r_49ca04 - x0 + 1, x1 - x0 + 1);
        y1 = y0 + sw_mulhi((y1 - y0) * 2, s_lt);
        x1 = r_49ca04;
        s_ldc = sw_mulhi(s_ldc * 2, s_lt);
    }
    if (y0 <= y1) {
        if (y1 > r_49ca08) {
            if (y0 > r_49ca08) {
                return;
            }
            s_lt = share(r_49ca08 - y0 + 1, y1 - y0 + 1);
            x1 = x0 + sw_mulhi((x1 - x0) * 2, s_lt);
            y1 = r_49ca08;
            s_ldc = sw_mulhi(s_ldc * 2, s_lt);
        }
        if (y0 < r_49ca00) {
            if (y1 < r_49ca00) {
                return;
            }
            s_lt = share(y1 - r_49ca00 + 1, y1 - y0 + 1);
            x0 = x1 - sw_mulhi((x1 - x0) * 2, s_lt);
            y0 = r_49ca00;
            t = s_lc + s_ldc;
            s_ldc = sw_mulhi(s_ldc * 2, s_lt);
            s_lc = t - s_ldc;
        }
    } else {
        if (y0 > r_49ca08) {
            if (y1 > r_49ca08) {
                return;
            }
            s_lt = share(r_49ca08 - y1 + 1, y0 - y1 + 1);
            x0 = x1 - sw_mulhi((x1 - x0) * 2, s_lt);
            y0 = r_49ca08;
            t = s_lc + s_ldc;
            s_ldc = sw_mulhi(s_ldc * 2, s_lt);
            s_lc = t - s_ldc;
        }
        if (y1 < r_49ca00) {
            if (y0 < r_49ca00) {
                return;
            }
            s_lt = share(y0 - r_49ca00 + 1, y0 - y1 + 1);
            x1 = x0 + sw_mulhi((x1 - x0) * 2, s_lt);
            y1 = r_49ca00;
            s_ldc = sw_mulhi(s_ldc * 2, s_lt);
        }
    }
    s_lx0 = x0;
    s_ly0 = y0;
    s_lx1 = x1;
    s_ly1 = y1;
    line_draw(x0, y0, x1, y1);
}

/* 0x44f0e9
 * Draws the context's commands in order into its target, clipped to its
 * rectangle. Returns -1 at a type it does not know, 0 at the end. */
int sw_raster_draw(const struct sw_raster *ctx)
{
    void *const *list = ctx->cmds;

    s_ctx = ctx;
    g_sw_dst = ctx->target;
    g_sw_point_table = ctx->table;
    r_49c9fc = r_49ca0c = ctx->x0;
    r_49ca00 = r_49ca10 = ctx->y0;
    r_49ca1c = g_sw_clip[0] = ctx->x0 << 8;
    r_49ca20 = g_sw_clip[1] = ctx->y0 << 8;
    r_49ca04 = ctx->x1;
    r_49ca08 = ctx->y1;
    r_49ca14 = ctx->x1 + 1;
    r_49ca18 = ctx->y1 + 1;
    g_sw_clip[2] = (ctx->x1 + 1) << 8;
    r_49ca24 = g_sw_clip[2] - 1;
    g_sw_clip[3] = (ctx->y1 + 1) << 8;
    r_49ca28 = g_sw_clip[3] - 1;
    for (; *list != NULL; list++) {
        const void *c = *list;

        switch (*(const s32 *)c) {
        case SW_POINT:
            sw_raster_point(c);
            break;
        case SW_POINT_FLAT:
            sw_raster_point_flat(c);
            break;
        case SW_POINT_TABLE:
            sw_raster_point_table(c);
            break;
        case SW_SPRITE:
            sw_raster_sprite(c);
            break;
        case SW_LINE:
            sw_raster_line(c);
            break;
        case SW_LINE_12:
            sw_raster_line_12(c);
            break;
        case SW_LINE_SHADE:
            line_shade(c);
            break;
        case SW_FLAT:
            sw_raster_flat(c);
            break;
        case SW_FLAT_16:
            sw_raster_flat16(c);
            break;
        case SW_TEXTURE:
            sw_raster_texture(c);
            break;
        case SW_TEXTURE_TABLE:
            sw_raster_texture_table(c);
            break;
        case SW_FLAT_TABLE:
            sw_raster_flat_table(c);
            break;
        case SW_PERSP:
        case SW_PERSP_TABLE:
            sw_raster_persp(c);
            break;
        case SW_TEXTURE_MIP:
            sw_raster_texture_mip(c);
            break;
        default:
            return -1;
        }
    }
    return 0;
}
