/*
 * soft3d/tex.c - the affine textured triangles: types 17 (0x452800),
 * 18 through a table (0x44ff60), 22 from a mip level (0x452050) and
 * sprites (0x455648), with the edge and span code they share
 * (0x64e000..0x64ea00 in the code segment, 0x4558c0, 0x4559a8).
 *
 * A triangle is sorted top, middle, bottom. 0x64e000 works out how u
 * and v change along a row. The edge that carries u and v is always the
 * right one: the long edge when it lies right of the middle vertex,
 * else the two short ones. Each row of it holds v, u and the pixel
 * where the span ends; the left edge holds where it starts. A span is
 * drawn from its right end leftwards, u and v stepping by -du/dx and
 * -dv/dx, packed into one register with v's integer and fraction in
 * the low 16 bits and u's fraction in the high 16, u's integer kept in
 * a byte beside them that takes the carry.
 *
 * Texture coordinates are 8.8 texels (16.16 once an edge has shifted
 * them); positions 8.8 pixels. The original draws the spans in four
 * byte stores aligned by self-modified jumps (the tables 0x452770
 * patches); the pixels and their order are the same one at a time.
 */

#include <stdint.h>

#include "soft3d/sw.h"

/* The triangle, sorted (0x4ba584): positions, then u and v. */
static struct {
    s32 xt, yt, xm, ym, xb, yb;         /* 0x4ba584 */
    s32 ut, vt, um, vm, ub, vb;         /* 0x4ba59c */
} s_tri;

static const u8 *s_tex;                 /* 0x4ba5bc, the page */
static const u8 *s_table;               /* 0x4ba580 */
static s32 s_width;                     /* 0x4ba5d0: the long edge's x less the middle's, at its row */
static s32 s_vlong;                     /* 0x4ba614 */
static s32 s_dudx, s_dvdx;              /* 0x4ba5c0, 0x4ba5c4, 16.16 a pixel */
static s32 s_ucorr[8], s_vcorr[8];      /* 0x4ba5d4, 0x4ba5f4: eighths of -du/dx, -dv/dx */
static u8 s_du_int;                     /* 0x4ba5b4: -du/dx's integer byte */
static u32 s_step;                      /* 0x4ba5b8: -du/dx's fraction, -dv/dx in 8.8 */

/* The edge being set up (0x63b4f0). */
static struct {
    s32 x0, y0, x1, y1;
    s32 u0, v0, u1, v1;
} s_edge;

static s32 s_eu, s_ev;                  /* 0x4ba618, 0x4ba61c: at the first row, 8.24 */
static s32 s_edu, s_edv;                /* 0x4ba620, 0x4ba624: a row */
static s32 s_ex;                        /* 0x4ba62c, 16.16 */
static s32 s_edx;                       /* 0x4ba630 */
static s32 *s_end;                      /* 0x4ba628: past the rows filled */
static s32 s_split;                     /* 0x4ba634: the edge crossed the right clip */
static s32 s_sx1, s_sy1, s_su1, s_sv1;  /* 0x4ba638..0x4ba644: its end beyond */

/* The edge buffers (0x63b518, 0x63b51c): the start row, the rows, then
 * a word a row on the left, three (v, u, x) on the right. */
static s32 s_left[0x968 / 4];
static s32 s_right[0x1c28 / 4];
static s32 s_ready;                     /* 0x4b3ce4 */

/* (k << 32) / n >> 1, an unsigned 1.31 share; the original faults
 * where the quotient overflows. */
static s32 share(s32 k, s32 n)
{
    if (n == 0) {
        return 0;
    }
    return (s32)((u32)(((u64)(u32)k << 32) / (u32)n) >> 1);
}

/* Bits 16..47 of a signed product. */
static s32 mul16(s32 a, s32 b)
{
    return (s32)(u32)(((int64_t)a * b) >> 16);
}

/* A 64 / 32 bit idiv, toward zero. */
static s32 idiv64(int64_t n, s32 d)
{
    if (d == 0) {
        return 0;
    }
    return (s32)(u32)(n / d);
}

static s32 shl8(s32 a)
{
    return (s32)((u32)a << 8);
}

/* 0x64e000
 * The width of the triangle at the middle row, and the u and v steps
 * along a row from it; then the steps negated for drawing leftwards,
 * packed, and in eighths for the edges' sub-pixel starts. */
static void gradients(void)
{
    s32 dy1 = s_tri.ym - s_tri.yt, dy2 = s_tri.yb - s_tri.yt, t, du, dv, a, nu, nv;
    int k;

    t = dy2 == dy1 ? 0x7fffffff : share(dy1, dy2);
    s_width = sw_sub(sw_mulhi(sw_mul(sw_sub(s_tri.xb, s_tri.xt), 2), t), sw_sub(s_tri.xm, s_tri.xt));
    s_vlong = sw_mulhi(sw_mul(sw_sub(s_tri.vb, s_tri.vt), 2), t);
    du = sw_add(sw_sub(sw_mulhi(sw_mul(sw_sub(s_tri.ub, s_tri.ut), 2), t), s_tri.um), s_tri.ut);
    a = (s_width >> 31) ^ s_width;
    dv = sw_add(sw_sub(s_vlong, s_tri.vm), s_tri.vt);
    if (a > 0x10000) {
        s_dudx = idiv64((int64_t)du * 65536, s_width);
        s_dvdx = idiv64((int64_t)dv * 65536, s_width);
    } else {
        s32 r;

        if (a > 3) {
            r = idiv64((int64_t)1 << 32, s_width);
        } else {
            r = s_width < 0 ? INT32_MIN : 0x7fffffff;
        }
        s_dudx = mul16(du, r);
        s_dvdx = mul16(dv, r);
    }
    nu = sw_sub(0, s_dudx);
    nv = sw_sub(0, s_dvdx);
    s_ucorr[0] = 0;
    s_vcorr[0] = 0;
    for (k = 1; k < 8; k++) {
        s_ucorr[k] = sw_add(s_ucorr[k - 1], nu >> 3);
        s_vcorr[k] = sw_add(s_vcorr[k - 1], nv >> 3);
    }
    s_du_int = (u8)((u32)nu >> 16);
    s_step = ((u32)nu << 16) | (((u32)nv >> 8) & 0xffff);
}

/* 0x64e790: an edge's steps a row for x, u and v, and their values at
 * the first row, a step from y0 less its fraction. */
static void edge_grad_uv(void)
{
    s32 dy = sw_sub(s_edge.y1, s_edge.y0), frac;

    if ((u32)dy <= 0x10000) {
        s32 r = (u32)dy <= 2 ? 0x7fffffff : (s32)(u32)(((u64)1 << 32) / (u32)dy);

        s_edv = mul16(sw_sub(s_edge.v1, s_edge.v0), r);
        s_edu = mul16(sw_sub(s_edge.u1, s_edge.u0), r);
        s_edx = mul16(sw_sub(s_edge.x1, s_edge.x0), r);
    } else {
        s_edv = idiv64((int64_t)sw_sub(s_edge.v1, s_edge.v0) * 65536, dy);
        s_edu = idiv64((int64_t)sw_sub(s_edge.u1, s_edge.u0) * 65536, dy);
        s_edx = idiv64((int64_t)sw_sub(s_edge.x1, s_edge.x0) * 65536, dy);
    }
    frac = 0x100 - (s_edge.y0 & 0xff);
    s_eu = sw_add(sw_mul(frac, s_edu) >> 8, shl8(s_edge.u0));
    s_ev = sw_add(sw_mul(frac, s_edv) >> 8, shl8(s_edge.v0));
    s_ex = sw_add(sw_mul(frac, s_edx >> 8), shl8(s_edge.x0));
}

/* 0x64ea00: the same for x alone. */
static void edge_grad_x(void)
{
    s32 dy = sw_sub(s_edge.y1, s_edge.y0), frac;

    if ((u32)dy <= 0x10000) {
        s32 r = (u32)dy <= 2 ? 0x7fffffff : (s32)(u32)(((u64)1 << 32) / (u32)dy);

        s_edx = mul16(sw_sub(s_edge.x1, s_edge.x0), r);
    } else {
        s_edx = idiv64((int64_t)sw_sub(s_edge.x1, s_edge.x0) * 65536, dy);
    }
    frac = 0x100 - (s_edge.y0 & 0xff);
    s_ex = sw_add(sw_mul(frac, s_edx >> 8), shl8(s_edge.x0));
}

/* 0x64e4e0: `n` rows of the right edge from the first: v and u less the
 * eighths of a pixel x lies right of its pixel, then the pixel. */
static s32 *fill_uv(s32 *p, s32 n)
{
    s32 u = s_eu, v = s_ev, x = s_ex;

    do {
        int k = (int)(((u32)x >> 13) & 7);

        p[1] = sw_add(s_ucorr[k], u);
        p[0] = sw_add(s_vcorr[k], v);
        u = sw_add(u, s_edu);
        p[2] = x >> 16;
        x = sw_add(x, s_edx);
        v = sw_add(v, s_edv);
        p += 3;
    } while (--n > 0);
    return p;
}

/* 0x64e470: `n` rows of the left edge, the pixel of x; the integer and
 * fraction step apart, the fraction's carry into the integer. */
static s32 *fill_x(s32 *p, s32 n)
{
    s32 xi = s_ex >> 16, di = s_edx >> 16;
    u32 xf = (u32)s_ex << 16, df = (u32)s_edx << 16;

    do {
        u32 f = xf + df;

        *p++ = xi;
        xi = sw_add(sw_add(xi, di), f < xf);
        xf = f;
    } while (--n > 0);
    return p;
}

/* 0x64e4a0: the same, a pixel left of the clip taken as the clip's. */
static s32 *fill_x_clip(s32 *p, s32 n)
{
    s32 xi = s_ex >> 16, di = s_edx >> 16;
    u32 xf = (u32)s_ex << 16, df = (u32)s_edx << 16;

    do {
        u32 f = xf + df;

        *p++ = sw_sub(xi, r_49ca0c) < 0 ? r_49ca0c : xi;
        xi = sw_add(sw_add(xi, di), f < xf);
        xf = f;
    } while (--n > 0);
    return p;
}

/* 0x64e200: the right edge, unclipped. Its start row is the left's. */
static void edge_uv(s32 *buf)
{
    s32 rows = (s_edge.y1 >> 8) - (s_edge.y0 >> 8);

    buf[1] = rows;
    if (rows != 0) {
        edge_grad_uv();
        fill_uv(buf + 2, rows);
    }
}

/* 0x64e240: the left edge, unclipped. */
static void edge_x(s32 *buf)
{
    s32 rows;

    buf[0] = s_edge.y0 >> 8;
    rows = (s_edge.y1 >> 8) - buf[0];
    buf[1] = rows;
    if (rows != 0) {
        edge_grad_x();
        fill_x(buf + 2, rows);
    }
}

/* 0x64e74b: the end of the right edge's second part moved in to the
 * right clip, u and v along the row with it. */
static void clamp_uv_end(void)
{
    if (g_sw_clip[2] < s_edge.x1) {
        s32 c = sw_sub(s_edge.x1, g_sw_clip[2]);

        s_edge.u1 = sw_sub(s_edge.u1, mul16(c, s_dudx));
        s_edge.v1 = sw_sub(s_edge.v1, mul16(c, s_dvdx));
        s_edge.x1 = g_sw_clip[2];
    }
}

static void save_end(void)
{
    s_sx1 = s_edge.x1;
    s_sy1 = s_edge.y1;
    s_su1 = s_edge.u1;
    s_sv1 = s_edge.v1;
}

/* 0x64e530
 * Cuts the right edge to the clip's rows, along the edge. Past the
 * clip's right side, x is held at it and u and v moved along the row;
 * an edge that crosses the side is split there, the part beyond kept
 * (s_split) for a second pass. */
static void clip_uv(void)
{
    s32 t;

    if (g_sw_clip[1] > s_edge.y0) {
        t = share(sw_sub(g_sw_clip[1], s_edge.y0), sw_sub(s_edge.y1, s_edge.y0));
        s_edge.v0 = sw_add(s_edge.v0, sw_mulhi(sw_mul(sw_sub(s_edge.v1, s_edge.v0), 2), t));
        s_edge.u0 = sw_add(s_edge.u0, sw_mulhi(sw_mul(sw_sub(s_edge.u1, s_edge.u0), 2), t));
        s_edge.x0 = sw_add(s_edge.x0, sw_mulhi(sw_mul(sw_sub(s_edge.x1, s_edge.x0), 2), t));
        s_edge.y0 = g_sw_clip[1];
    }
    if (g_sw_clip[3] < s_edge.y1) {
        t = share(sw_sub(s_edge.y1, g_sw_clip[3]), sw_sub(s_edge.y1, s_edge.y0));
        s_edge.v1 = sw_sub(s_edge.v1, sw_mulhi(sw_mul(sw_sub(s_edge.v1, s_edge.v0), 2), t));
        s_edge.u1 = sw_sub(s_edge.u1, sw_mulhi(sw_mul(sw_sub(s_edge.u1, s_edge.u0), 2), t));
        s_edge.x1 = sw_sub(s_edge.x1, sw_mulhi(sw_mul(sw_sub(s_edge.x1, s_edge.x0), 2), t));
        s_edge.y1 = g_sw_clip[3];
    }
    s_split = 0;
    if (g_sw_clip[2] < s_edge.x0) {
        s32 c;

        if (g_sw_clip[2] <= s_edge.x1) {
            c = sw_sub(s_edge.x1, g_sw_clip[2]);
            s_edge.u1 = sw_sub(s_edge.u1, mul16(c, s_dudx));
            s_edge.v1 = sw_sub(s_edge.v1, mul16(c, s_dvdx));
            s_edge.x1 = g_sw_clip[2];
        } else {
            s_split = 1;
            save_end();
            t = share(sw_sub(g_sw_clip[2], s_edge.x1), sw_sub(s_edge.x0, s_edge.x1));
            s_edge.u1 = sw_sub(s_edge.u1, sw_mulhi(sw_mul(sw_sub(s_edge.u1, s_edge.u0), 2), t));
            s_edge.v1 = sw_sub(s_edge.v1, sw_mulhi(sw_mul(sw_sub(s_edge.v1, s_edge.v0), 2), t));
            s_edge.y1 = sw_sub(s_edge.y1, sw_mulhi(sw_mul(sw_sub(s_edge.y1, s_edge.y0), 2), t));
            s_edge.x1 = g_sw_clip[2];
        }
        c = sw_sub(s_edge.x0, g_sw_clip[2]);
        s_edge.u0 = sw_sub(s_edge.u0, mul16(c, s_dudx));
        s_edge.v0 = sw_sub(s_edge.v0, mul16(c, s_dvdx));
        s_edge.x0 = g_sw_clip[2];
    }
    if (g_sw_clip[2] < s_edge.x1) {
        s32 n = sw_sub(s_edge.x1, s_edge.x0), k = sw_sub(s_edge.x1, g_sw_clip[2]);

        s_split = 1;
        save_end();
        t = k == n ? 0x7fffffff : share(k, n);
        s_edge.u1 = sw_sub(s_edge.u1, sw_mulhi(sw_mul(sw_sub(s_edge.u1, s_edge.u0), 2), t));
        s_edge.v1 = sw_sub(s_edge.v1, sw_mulhi(sw_mul(sw_sub(s_edge.v1, s_edge.v0), 2), t));
        s_edge.y1 = sw_sub(s_edge.y1, sw_mulhi(sw_mul(sw_sub(s_edge.y1, s_edge.y0), 2), t));
        s_edge.x1 = g_sw_clip[2];
    }
}

/* 0x64e270: the right edge, clipped; none for an edge outside the
 * clip's rows. */
static void edge_uv_clip(s32 *buf)
{
    s32 rows;

    if (!(g_sw_clip[1] < s_edge.y1) || !(g_sw_clip[3] > s_edge.y0)) {
        buf[1] = 0;
        return;
    }
    clip_uv();
    s_end = buf + 2;
    rows = (s_edge.y1 >> 8) - (s_edge.y0 >> 8);
    buf[1] = rows;
    if (rows != 0) {
        edge_grad_uv();
        s_end = fill_uv(buf + 2, rows);
    }
    if (s_split == 1) {
        s_edge.x0 = s_edge.x1;
        s_edge.y0 = s_edge.y1;
        s_edge.u0 = s_edge.u1;
        s_edge.v0 = s_edge.v1;
        s_edge.x1 = s_sx1;
        s_edge.y1 = s_sy1;
        s_edge.u1 = s_su1;
        s_edge.v1 = s_sv1;
        clamp_uv_end();
        rows = (s_edge.y1 >> 8) - (s_edge.y0 >> 8);
        if (rows != 0) {
            buf[1] += rows;
            edge_grad_uv();
            fill_uv(s_end, rows);
        }
    }
}

/* 0x64e8a0: 0x64e530 for the left edge, x alone. */
static void clip_x(void)
{
    s32 t;

    if (g_sw_clip[1] > s_edge.y0) {
        t = share(sw_sub(g_sw_clip[1], s_edge.y0), sw_sub(s_edge.y1, s_edge.y0));
        s_edge.x0 = sw_add(s_edge.x0, sw_mulhi(sw_mul(sw_sub(s_edge.x1, s_edge.x0), 2), t));
        s_edge.y0 = g_sw_clip[1];
    }
    if (g_sw_clip[3] < s_edge.y1) {
        t = share(sw_sub(s_edge.y1, g_sw_clip[3]), sw_sub(s_edge.y1, s_edge.y0));
        s_edge.x1 = sw_sub(s_edge.x1, sw_mulhi(sw_mul(sw_sub(s_edge.x1, s_edge.x0), 2), t));
        s_edge.y1 = g_sw_clip[3];
    }
    s_split = 0;
    if (g_sw_clip[2] < s_edge.x0) {
        if (g_sw_clip[2] <= s_edge.x1) {
            s_edge.x1 = g_sw_clip[2];
        } else {
            s_split = 1;
            s_sx1 = s_edge.x1;
            s_sy1 = s_edge.y1;
            t = share(sw_sub(g_sw_clip[2], s_edge.x1), sw_sub(s_edge.x0, s_edge.x1));
            s_edge.y1 = sw_sub(s_edge.y1, sw_mulhi(sw_mul(sw_sub(s_edge.y1, s_edge.y0), 2), t));
            s_edge.x1 = g_sw_clip[2];
        }
        s_edge.x0 = g_sw_clip[2];
    }
    if (g_sw_clip[2] < s_edge.x1) {
        s32 n = sw_sub(s_edge.x1, s_edge.x0), k = sw_sub(s_edge.x1, g_sw_clip[2]);

        s_split = 1;
        s_sy1 = s_edge.y1;
        s_sx1 = s_edge.x1;
        t = k == n ? 0x7fffffff : share(k, n);
        s_edge.y1 = sw_sub(s_edge.y1, sw_mulhi(sw_mul(sw_sub(s_edge.y1, s_edge.y0), 2), t));
        s_edge.x1 = g_sw_clip[2];
    }
}

/* 0x64e9da */
static void clamp_x_end(void)
{
    if (g_sw_clip[2] < s_edge.x1) {
        s_edge.x1 = g_sw_clip[2];
    }
}

/* 0x64e380: the left edge, clipped. An edge above or below the clip's
 * rows starts at the clip's top or bottom row with none. */
static void edge_x_clip(s32 *buf)
{
    s32 rows;

    if (!(g_sw_clip[1] < s_edge.y1)) {
        buf[1] = 0;
        buf[0] = r_49ca10;
        return;
    }
    if (!(g_sw_clip[3] > s_edge.y0)) {
        buf[1] = 0;
        buf[0] = r_49ca18;
        return;
    }
    clip_x();
    s_end = buf + 2;
    buf[0] = s_edge.y0 >> 8;
    rows = (s_edge.y1 >> 8) - buf[0];
    buf[1] = rows;
    if (rows != 0) {
        edge_grad_x();
        s_end = fill_x_clip(buf + 2, rows);
    }
    if (s_split == 1) {
        s_edge.x0 = s_edge.x1;
        s_edge.y0 = s_edge.y1;
        s_edge.x1 = s_sx1;
        s_edge.y1 = s_sy1;
        clamp_x_end();
        rows = (s_edge.y1 >> 8) - (s_edge.y0 >> 8);
        if (rows != 0) {
            buf[1] += rows;
            edge_grad_x();
            fill_x_clip(s_end, rows);
        }
    }
}

/* Where a span drawer has got to: the left and right edges' next rows
 * and the next row of the screen. */
struct spans {
    const s32 *left, *right;
    u8 *dst;
};

/* The packed position: u's fraction high, v's integer and fraction
 * low (ecx), u's integer (bl). */
struct texel {
    u32 pos;
    u8 u;
};

static struct texel texel_start(const s32 *r)
{
    struct texel t;

    t.pos = ((u32)r[1] << 16) | (((u32)r[0] >> 8) & 0xffff);
    t.u = (u8)((u32)r[1] >> 16);
    return t;
}

/* The texel under the position, then a step left. */
static u8 texel_next(struct texel *t)
{
    u8 c = s_tex[((t->pos >> 8) & 0xff) << 8 | t->u];
    u32 p = t->pos + s_step;

    t->u = (u8)(t->u + s_du_int + (p < t->pos));
    t->pos = p;
    return c;
}

/* 0x4559a8: `rows` spans of texels, each from its right end at the
 * right edge's pixel (not drawn) leftwards to the left edge's. */
static void spans_texel(struct spans *s, s32 rows)
{
    for (; rows > 0; rows--) {
        s32 l = s->left[0], n = sw_sub(s->right[2], l);

        if (n >= 0) {
            struct texel t = texel_start(s->right);
            u8 *p = s->dst + l;

            while (n > 0) {
                n--;
                p[n] = texel_next(&t);
            }
        }
        s->dst += g_sw_pitch;
        s->right += 3;
        s->left++;
    }
}

/* 0x4558c0: the same, each pixel table[texel << 8 | dst]. */
static void spans_table(struct spans *s, s32 rows)
{
    for (; rows > 0; rows--) {
        s32 l = s->left[0], n = sw_sub(sw_sub(s->right[2], l), 1);

        if (n >= 0) {
            struct texel t = texel_start(s->right);
            u8 *p = s->dst + l;

            for (; n >= 0; n--) {
                u8 d = p[n];

                p[n] = s_table[texel_next(&t) << 8 | d];
            }
        }
        s->dst += g_sw_pitch;
        s->right += 3;
        s->left++;
    }
}

enum { SPAN_TEXEL, SPAN_TABLE };

static void spans(struct spans *s, s32 rows, int kind)
{
    if (kind == SPAN_TABLE) {
        spans_table(s, rows);
    } else {
        spans_texel(s, rows);
    }
}

static void edge_set(s32 x0, s32 y0, s32 x1, s32 y1)
{
    s_edge.x0 = x0;
    s_edge.y0 = y0;
    s_edge.x1 = x1;
    s_edge.y1 = y1;
}

static void edge_set_uv(s32 u0, s32 v0, s32 u1, s32 v1)
{
    s_edge.u0 = u0;
    s_edge.v0 = v0;
    s_edge.u1 = u1;
    s_edge.v1 = v1;
}

/* The body 0x452800, 0x44ff60 and 0x452050 share. xy holds the three
 * vertices' x and y, uv their u and v; `mip` takes u and v as
 * (c & 0x3fff) << 2. */
static void triangle(const s32 *xy, const s32 *uv, const u8 *tex, const u8 *table, int kind,
                     int mip)
{
    s32 maxx, minx, top = 0, mid = 1, bot = 2, ybot, ytop;
    s32 uvv[6];
    int clipped, i;
    struct spans s;

    if (xy[0] > xy[2]) {
        maxx = xy[0];
        minx = xy[2];
    } else {
        maxx = xy[2];
        minx = xy[0];
    }
    if (xy[4] > maxx) {
        maxx = xy[4];
    } else if (minx > xy[4]) {
        minx = xy[4];
    }
    if (xy[5] < xy[1]) {
        top = 2;
        bot = 0;
    }
    if (xy[2 * top + 1] > xy[3]) {
        mid = top;
        top = 1;
    } else if (xy[2 * bot + 1] < xy[3]) {
        mid = bot;
        bot = 1;
    }
    ybot = xy[2 * bot + 1];
    ytop = xy[2 * top + 1];
    if ((sw_sub(g_sw_clip[3], ybot) | sw_sub(g_sw_clip[2], maxx) | sw_sub(ytop, g_sw_clip[1])
         | sw_sub(minx, g_sw_clip[0])) < 0) {
        if ((sw_sub(g_sw_clip[2], minx) | sw_sub(g_sw_clip[3], ytop) | sw_sub(ybot, g_sw_clip[1])
             | sw_sub(maxx, g_sw_clip[0])) < 0) {
            return;
        }
        clipped = 1;
    } else {
        clipped = 0;
    }
    for (i = 0; i < 6; i++) {
        uvv[i] = mip ? (s32)(((u32)uv[i] & 0x3fff) << 2) : uv[i];
    }
    s_tex = tex;
    s_table = table;
    s_tri.xt = xy[2 * top];
    s_tri.yt = xy[2 * top + 1];
    s_tri.xm = xy[2 * mid];
    s_tri.ym = xy[2 * mid + 1];
    s_tri.xb = xy[2 * bot];
    s_tri.yb = xy[2 * bot + 1];
    s_tri.ut = uvv[2 * top];
    s_tri.vt = uvv[2 * top + 1];
    s_tri.um = uvv[2 * mid];
    s_tri.vm = uvv[2 * mid + 1];
    s_tri.ub = uvv[2 * bot];
    s_tri.vb = uvv[2 * bot + 1];
    gradients();

    if (s_width > 0) {
        /* The long edge on the right carries u and v; the left is the
         * two short edges. */
        edge_set(s_tri.xt, s_tri.yt, s_tri.xm, s_tri.ym);
        if (clipped) {
            edge_x_clip(s_left);
        } else {
            edge_x(s_left);
        }
        edge_set(s_tri.xt, s_tri.yt, s_tri.xb, s_tri.yb);
        edge_set_uv(s_tri.ut, s_tri.vt, s_tri.ub, s_tri.vb);
        if (clipped) {
            edge_uv_clip(s_right);
        } else {
            edge_uv(s_right);
        }
        s.dst = g_sw_dst + sw_mul(s_left[0], g_sw_pitch);
        s.left = s_left + 2;
        s.right = s_right + 2;
        spans(&s, s_left[1], kind);
        if (clipped) {
            edge_set(s_tri.xm, s_tri.ym, s_tri.xb, s_tri.yb);
            edge_x_clip(s_left);
        } else {
            s_edge.x0 = s_tri.xm;
            s_edge.y0 = s_tri.ym;
            edge_x(s_left);
        }
        s.left = s_left + 2;
        spans(&s, s_left[1], kind);
    } else {
        /* The long edge on the left; the short ones carry u and v. */
        edge_set(s_tri.xt, s_tri.yt, s_tri.xm, s_tri.ym);
        edge_set_uv(s_tri.ut, s_tri.vt, s_tri.um, s_tri.vm);
        if (clipped) {
            edge_uv_clip(s_right);
        } else {
            edge_uv(s_right);
        }
        edge_set(s_tri.xt, s_tri.yt, s_tri.xb, s_tri.yb);
        if (clipped) {
            edge_x_clip(s_left);
        } else {
            edge_x(s_left);
        }
        s.dst = g_sw_dst + sw_mul(s_left[0], g_sw_pitch);
        s.left = s_left + 2;
        s.right = s_right + 2;
        spans(&s, s_right[1], kind);
        edge_set(s_tri.xm, s_tri.ym, s_tri.xb, s_tri.yb);
        edge_set_uv(s_tri.um, s_tri.vm, s_tri.ub, s_tri.vb);
        if (clipped) {
            edge_uv_clip(s_right);
        } else {
            edge_uv(s_right);
        }
        s.right = s_right + 2;
        spans(&s, s_right[1], kind);
    }
}

static void unpack(s32 xy[6], const void *v)
{
    const s32 *p = v;
    int i;

    for (i = 0; i < 6; i++) {
        xy[i] = p[i];
    }
}

/* 0x452800 */
void sw_raster_texture(const struct sw_cmd_texture *c)
{
    s32 xy[6];

    unpack(xy, c->v);
    triangle(xy, c->uv, c->tex, NULL, SPAN_TEXEL, 0);
}

/* 0x44ff60 */
void sw_raster_texture_table(const struct sw_cmd_texture_table *c)
{
    s32 xy[6];

    unpack(xy, c->v);
    triangle(xy, c->uv, c->tex, c->table, SPAN_TABLE, 0);
}

/* 0x452050: u and v from a mip level, as (c & 0x3fff) << 2. */
void sw_raster_texture_mip(const struct sw_cmd_texture *c)
{
    s32 xy[6];

    unpack(xy, c->v);
    triangle(xy, c->uv, c->tex, NULL, SPAN_TEXEL, 1);
}

/* 0x455648
 * A sprite: its box of texels turned and scaled by the 2 x 2 matrix
 * (8.8 pixels a texel, rows sx f38, f3c sy), the hot spot at (x, y),
 * drawn as two triangles through its table (0x44ff60). */
void sw_raster_sprite(const struct sw_cmd_sprite *c)
{
    static struct sw_cmd_texture_table s_cmd;   /* 0x4ba468 */
    static s32 s_uv[6];                         /* 0x4ba490 */
    const struct sw_sprite_src *src = c->src;
    const s32 *m = &c->scale->sx;   /* sx, f38, f3c, sy */
    s32 nx = sw_sub(0, src->hot_x), ny = sw_sub(0, src->hot_y);
    s32 du = sw_sub(src->u1, src->u0), dv = sw_sub(src->v1, src->v0);
    s32 ax, ay, bx, by, cx, cy, dx, dy, ex, ey;

    /* 0x4ba430..0x4ba44c: the corners (u0 v0), (u1 v0), (u0 v1),
     * (u1 v1). */
    ax = sw_add(mul16(nx, m[0]), mul16(ny, m[2]));
    ay = sw_add(mul16(nx, m[1]), mul16(ny, m[3]));
    ey = mul16(m[1], du);
    ex = mul16(m[0], du);
    cy = sw_add(mul16(m[3], dv), ay);
    dy = sw_add(cy, ey);
    by = sw_add(ey, ay);
    cx = sw_add(mul16(m[2], dv), ax);
    dx = sw_add(cx, ex);
    bx = sw_add(ex, ax);
    ax = sw_add(ax, c->x);
    ay = sw_add(ay, c->y);
    bx = sw_add(bx, c->x);
    by = sw_add(by, c->y);
    cx = sw_add(cx, c->x);
    cy = sw_add(cy, c->y);
    dx = sw_add(dx, c->x);
    dy = sw_add(dy, c->y);

    s_cmd.v[0].x = ax;
    s_cmd.v[0].y = ay;
    s_cmd.v[1].x = bx;
    s_cmd.v[1].y = by;
    s_cmd.v[2].x = cx;
    s_cmd.v[2].y = cy;
    s_cmd.uv = s_uv;
    s_uv[0] = src->u0;
    s_uv[1] = src->v0;
    s_uv[2] = src->u1;
    s_uv[3] = src->v0;
    s_uv[4] = src->u0;
    s_uv[5] = src->v1;
    s_cmd.tex = src->tex;
    s_cmd.table = src->table;
    sw_raster_texture_table(&s_cmd);

    s_cmd.v[0].x = bx;
    s_cmd.v[0].y = by;
    s_cmd.v[1].x = cx;
    s_cmd.v[1].y = cy;
    s_cmd.v[2].x = dx;
    s_cmd.v[2].y = dy;
    s_cmd.uv = s_uv;
    s_uv[0] = src->u1;
    s_uv[1] = src->v0;
    s_uv[2] = src->u0;
    s_uv[3] = src->v1;
    s_uv[4] = src->u1;
    s_uv[5] = src->v1;
    s_cmd.tex = src->tex;
    s_cmd.table = src->table;
    sw_raster_texture_table(&s_cmd);
}

/* 0x44ff20: allocates the edge buffers once; here they are static. */
void sw_hook_44ff20(int w, int h)
{
    s_ready = 1;
}

/* 0x452770: the same, and patches 0x4559a8's jump tables for its span
 * ends and alignment, which the C spans do not need. */
void sw_hook_452770(int w, int h)
{
    s_ready = 1;
}

/* 0x452010 */
void sw_hook_452010(int w, int h)
{
    s_ready = 1;
}
