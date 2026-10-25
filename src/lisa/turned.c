/*
 * lisa/turned.c - the Lisa library's textured triangles for the 2D: a
 * scaled or turned sprite is drawn as two of them (0x468c70, 0x45ca50),
 * and the gfx slot 0x50ebb8 draws lists of them (0x45d8a0).
 *
 * A triangle is sorted top to bottom. Its u and v change across a row
 * by du/dx and dv/dx, found at the middle vertex's row (0x64ea80). Each
 * edge is stepped row by row into a buffer of (v, u, x) entries; the
 * edge on the right carries u and v, corrected to the pixel where its
 * row ends by eighths of a pixel (0x64f220). A row is drawn from its
 * right end leftwards, u and v stepped in one packed register (v in
 * 8.8 in the low 16 bits, u's fraction in the high 16, u's integer in
 * a byte that takes the carry), and the pixel at the left edge takes
 * the left edge's own texel. With the triangle partly off the clip, the
 * edges are cut to its rows and right side, and each row to its left
 * side (0x45d170).
 *
 * Positions are 8.8 pixels, texture coordinates 8.8 texels of a 256 x
 * 256 page. The original keeps the routines that step the edges in its
 * `code` segment (0x64ea80..0x64f4d0).
 */

#include <stdlib.h>
#include <string.h>

#include "lisa/gfx.h"
#include "lisa/turned.h"

/* The edge buffers (0x51a9d8, 0x51a9dc, 0x51a9e0): the first row, the
 * rows, then (v, u, x) a row. */
enum { EDGE_BYTES = 0x20d8, EDGE_ROWS = (EDGE_BYTES - 8) / 12 };

static s32 *r_51a9d8, *r_51a9dc, *r_51a9e0;
static s32 r_51a9cc, r_51aa50;
static s32 r_4bace4, r_4bace8;          /* the buffers are made */
static s32 *r_51aa5c, *r_51aa60, *r_51aa64;     /* 0x45d8a0's */

/* The triangle, sorted (0x4bad74): x0 y0 x1 y1 x2 y2 u0 v0 u1 v1 u2 v2. */
static s32 r_4bad74[12];
static const u8 *r_51a9c4;              /* the page */
static u8 *r_51a9d0;                    /* the row being drawn */
static s32 r_51a9d4, r_51a9f8;          /* the least and most x */

/* 0x64ea80's: du/dx and dv/dx in 16.16 a pixel, and their eighths. */
static s32 r_4bada4, r_4bada8;
static s32 r_4badb4[8], r_4badd4[8];
static s32 r_4badf4, r_4badf8, r_4badfc;
static s32 r_4bae00, r_4bae04, r_4bae08, r_4bae0c;
static u32 r_4bae10;

/* An edge (0x51a9a0, 0x51aa00): x0 y0 x1 y1 u0 v0 u1 v1. */
static s32 s_edge[8];

/* 0x64f4d0's: u, v and x at the first row, and their steps a row. */
static s32 r_4baea4, r_4baea8, r_4baeac, r_4baeb0, r_4baebc, r_4baec0;

/* 0x64f270's: an edge that crosses the right side, and its end. */
static s32 r_4baec8;
static s32 r_4baecc, r_4baed0, r_4baed4, r_4baed8;

/* The spans' state (0x4bad60, 0x4bad64, 0x4bad68, 0x4bad6c, 0x4bad70),
 * and the same for 0x468030's (0x4baefc..0x4baf0c). */
static u8 r_4bad60;
static u32 r_4bad64;
static const u8 *r_4bad68, *r_4bad6c;
static u8 r_4bad70;

/* The clip, 8.8 (0x63f2cc, 0x63f2b0, 0x63f2d0, 0x63f2c4). */
static s32 clip_x0_8(void)
{
    return (s32)((u32)g_gfx_target.clip_x0 << 8);
}

static s32 clip_y0_8(void)
{
    return (s32)((u32)g_gfx_target.clip_y0 << 8);
}

static s32 clip_x1_8(void)
{
    return (s32)((u32)g_gfx_target.clip_x1 << 8);
}

static s32 clip_y1_8(void)
{
    return (s32)((u32)g_gfx_target.clip_y1 << 8);
}

/* imul's edx, and its bits 16..47 (mov ax, dx; ror eax, 16). */
static s32 mul_hi(s32 a, s32 b)
{
    return (s32)(((int64_t)a * b) >> 32);
}

static s32 mul_mid(s32 a, s32 b)
{
    return (s32)(u32)(((int64_t)a * b) >> 16);
}

static s32 wrap_mul(s32 a, s32 b)
{
    return (s32)((u32)a * (u32)b);
}

/* idiv of a 64 bit dividend; a quotient that does not fit, which
 * faults in the original, is cut to 32 bits, and 0 gives 0. */
static s32 idiv64(int64_t n, s32 d)
{
    if (d == 0) {
        return 0;
    }
    if (n == INT64_MIN && d == -1) {
        return 0;
    }
    return (s32)(u32)(n / d);
}

/* (k << 32) / n >> 1, as div does it: the share k of n in 1.31. */
static u32 share(u32 k, u32 n)
{
    if (n == 0 || k >= n) {
        return 0x7fffffff;
    }
    return (u32)((((u64)k << 32) / n) >> 1);
}

static s32 *edge_buffer(void)
{
    s32 *b = malloc(EDGE_BYTES);

    if (b == NULL) {
        abort();
    }
    return b;
}

/* 0x45d840 */
void gfx_init_45d840(void)
{
    r_51aa50 = 0;
    if (r_4bace8 == 0) {
        r_51aa5c = edge_buffer();
        r_51aa60 = edge_buffer();
        r_51aa64 = edge_buffer();
        r_4bace8 = 1;
    }
}

/* 0x45c9f0 */
void gfx_init_spans(void)
{
    r_51a9cc = 0;
    if (r_4bace4 == 0) {
        r_51a9d8 = edge_buffer();
        r_51a9dc = edge_buffer();
        r_51a9e0 = edge_buffer();
        r_4bace4 = 1;
    }
}

/* 0x64ea80: du/dx and dv/dx, from the long edge's u and v at the middle
 * vertex's row against the middle vertex's; and the eighths of them
 * the right edge's rows are corrected by. */
static void gradients(void)
{
    const s32 *t = r_4bad74;
    s32 e, k;

    r_4badf4 = t[3] - t[1];
    r_4badf8 = t[5] - t[1];
    if (r_4badf8 == 0) {
        r_4badf8 = 1;
    }
    r_4bae10 = r_4badf8 == r_4badf4 ? 0x7fffffffu : share((u32)r_4badf4, (u32)r_4badf8);
    r_4bae00 = t[2] - t[0];
    r_4bae04 = mul_hi(wrap_mul(t[4] - t[0], 2), (s32)r_4bae10);
    r_4bae08 = mul_hi(wrap_mul(t[10] - t[6], 2), (s32)r_4bae10);
    r_4bae0c = mul_hi(wrap_mul(t[11] - t[7], 2), (s32)r_4bae10);
    e = r_4bae04 - r_4bae00;
    if (e >= -2 && e <= 2) {
        e = (s32)((u32)((e >> 2) | 1) << 2);
    }
    r_4badfc = e;
    r_4bada4 = idiv64((int64_t)(r_4bae08 - t[8] + t[6]) * 65536, r_4badfc);
    e = -r_4bada4 >> 3;
    for (k = 0; k < 8; k++) {
        r_4badb4[k] = e * k;
    }
    r_4bada8 = idiv64((int64_t)(r_4bae0c - t[9] + t[7]) * 65536, r_4badfc);
    e = -r_4bada8 >> 3;
    for (k = 0; k < 8; k++) {
        r_4badd4[k] = e * k;
    }
}

/* The quotient 0x64f4d0 takes for u and v: the dividend's high word is
 * the difference's sign, its low word the difference << 16. */
static s32 step_of(s32 d, s32 rows)
{
    int64_t n = (int64_t)(((u64)(u32)(d >> 31) << 32) | (u32)((u32)d << 16));

    return idiv64(n, rows);
}

/* 0x64f4d0: an edge's steps a row for u, v and x, and its values at
 * the first row's middle. The height is 8.8; one under a quarter row
 * counts as 0x41, or as 3 when the edge is wider than 256 pixels. */
static void edge_steps(const s32 *e)
{
    s32 h = e[3] - e[1], f;

    if ((u32)h <= 0x40) {
        s32 w = e[2] - e[0];

        if (w <= 0) {
            w = -w;
        }
        if ((u32)w <= 0x10000) {
            h = 0x41;
        } else if ((u32)h <= 3) {
            h = 3;
        }
    }
    r_4baeb0 = step_of(e[7] - e[5], h);
    r_4baeac = step_of(e[6] - e[4], h);
    r_4baec0 = idiv64((int64_t)(e[2] - e[0]) * 65536, h);
    f = 0x100 - (e[1] & 0xff);
    r_4baea4 = (wrap_mul(f, r_4baeac) >> 8) + (s32)((u32)e[4] << 8);
    r_4baea8 = (wrap_mul(f, r_4baeb0) >> 8) + (s32)((u32)e[5] << 8);
    r_4baebc = wrap_mul(f, r_4baec0 >> 8) + (s32)((u32)e[0] << 8);
}

/* 0x64f1e0: `n` rows of an edge of x alone (u and v kept whole), from
 * entry `at`. The original counts n in 16 bits. */
static void fill_plain(s32 *buf, int at, s32 n, s32 u, s32 v, s32 x)
{
    s16 c = (s16)n;

    do {
        if (at < EDGE_ROWS) {
            s32 *o = buf + 2 + 3 * at;

            o[1] = u >> 16;
            o[0] = v >> 16;
            o[2] = x >> 16;
        }
        at++;
        u += r_4baeac;
        v += r_4baeb0;
        x += r_4baec0;
        c--;
    } while (c > 0);
}

/* 0x64f220: `n` rows of the edge that carries u and v, each corrected
 * back by the eighths of a pixel the edge's x is past its pixel. */
static void fill_corrected(s32 *buf, int at, s32 n, s32 u, s32 v, s32 x)
{
    do {
        int k = ((u32)x >> 13) & 7;

        if (at < EDGE_ROWS) {
            s32 *o = buf + 2 + 3 * at;

            o[1] = (r_4badb4[k] + u) >> 8;
            o[0] = (r_4badd4[k] + v) >> 8;
            o[2] = x >> 16;
        }
        at++;
        u += r_4baeac;
        v += r_4baeb0;
        x += r_4baec0;
        n--;
    } while (n > 0);
}

/* 0x64ec00: an edge that carries u and v, unclipped. */
static void edge_uv(s32 *buf, const s32 *e)
{
    buf[0] = e[1] >> 8;
    buf[1] = (e[3] >> 8) - buf[0];
    if (buf[1] != 0) {
        edge_steps(e);
        fill_corrected(buf, 0, buf[1], r_4baea4, r_4baea8, r_4baebc);
    }
}

/* 0x64ec40: an edge of x alone, unclipped. */
static void edge_x(s32 *buf, const s32 *e)
{
    buf[0] = e[1] >> 8;
    buf[1] = (e[3] >> 8) - buf[0];
    if (buf[1] != 0) {
        edge_steps(e);
        fill_plain(buf, 0, buf[1], r_4baea4, r_4baea8, r_4baebc);
    }
}

/* 0x64f489: the end of an edge's second part, past the right side,
 * moved back to it, its u and v by du/dx and dv/dx. */
static void clamp_end(s32 *e)
{
    if (clip_x1_8() < e[2]) {
        s32 d = e[2] - clip_x1_8();

        e[6] -= mul_mid(d, r_4bada4);
        e[7] -= mul_mid(d, r_4bada8);
        e[2] = clip_x1_8();
    }
}

/* 0x64f270
 * Cuts an edge to the clip's rows, and to its right side: an end past
 * it is moved onto it, u and v along the row; an edge that crosses it
 * is split, its part beyond kept for later (0x4baec8, 0x4baecc). */
static void edge_clip(s32 *e)
{
    u32 t;
    s32 cx1 = clip_x1_8();

    if (clip_y0_8() > e[1]) {
        t = share((u32)(clip_y0_8() - e[1]), (u32)(e[3] - e[1]));
        e[5] += mul_hi(wrap_mul(e[7] - e[5], 2), (s32)t);
        e[4] += mul_hi(wrap_mul(e[6] - e[4], 2), (s32)t);
        e[0] += mul_hi(wrap_mul(e[2] - e[0], 2), (s32)t);
        e[1] = clip_y0_8();
    }
    if (clip_y1_8() < e[3]) {
        t = share((u32)(e[3] - clip_y1_8()), (u32)(e[3] - e[1]));
        e[7] -= mul_hi(wrap_mul(e[7] - e[5], 2), (s32)t);
        e[6] -= mul_hi(wrap_mul(e[6] - e[4], 2), (s32)t);
        e[2] -= mul_hi(wrap_mul(e[2] - e[0], 2), (s32)t);
        e[3] = clip_y1_8();
    }
    r_4baec8 = 0;
    if (cx1 < e[0]) {
        if (cx1 <= e[2]) {
            s32 d = e[2] - cx1;

            e[6] -= mul_mid(d, r_4bada4);
            e[7] -= mul_mid(d, r_4bada8);
            e[2] = cx1;
        } else {
            r_4baec8 = 1;
            r_4baecc = e[2];
            r_4baed0 = e[3];
            r_4baed4 = e[6];
            r_4baed8 = e[7];
            t = share((u32)(cx1 - e[2]), (u32)(e[0] - e[2]));
            e[6] -= mul_hi(wrap_mul(e[6] - e[4], 2), (s32)t);
            e[7] -= mul_hi(wrap_mul(e[7] - e[5], 2), (s32)t);
            e[3] -= mul_hi(wrap_mul(e[3] - e[1], 2), (s32)t);
            e[2] = cx1;
        }
        {
            s32 d = e[0] - cx1;

            e[4] -= mul_mid(d, r_4bada4);
            e[5] -= mul_mid(d, r_4bada8);
            e[0] = cx1;
        }
    }
    if (cx1 < e[2]) {
        u32 n = (u32)(e[2] - e[0]), k = (u32)(e[2] - cx1);

        r_4baec8 = 1;
        r_4baecc = e[2];
        r_4baed0 = e[3];
        r_4baed4 = e[6];
        r_4baed8 = e[7];
        t = k == n ? 0x7fffffffu : share(k, n);
        e[6] -= mul_hi(wrap_mul(e[6] - e[4], 2), (s32)t);
        e[7] -= mul_hi(wrap_mul(e[7] - e[5], 2), (s32)t);
        e[3] -= mul_hi(wrap_mul(e[3] - e[1], 2), (s32)t);
        e[2] = cx1;
    }
}

/* The second part of a split edge: from where the first ended to the
 * end kept, moved onto the right side (0x64efa0, 0x64f0c0). */
static s32 edge_second(s32 *e)
{
    e[0] = e[2];
    e[1] = e[3];
    e[4] = e[6];
    e[5] = e[7];
    e[2] = r_4baecc;
    e[3] = r_4baed0;
    e[6] = r_4baed4;
    e[7] = r_4baed8;
    clamp_end(e);
    return (e[3] >> 8) - (e[1] >> 8);
}

/* 0x64efa0: an edge that carries u and v, clipped. An edge above or
 * below the clip has no rows, its first row the clip's. */
static void edge_uv_clip(s32 *buf, s32 *e)
{
    int next;
    s32 n;

    if (clip_y0_8() >= e[3]) {
        buf[1] = 0;
        buf[0] = g_gfx_target.clip_y0;
        return;
    }
    if (clip_y1_8() <= e[1]) {
        buf[1] = 0;
        buf[0] = g_gfx_target.clip_y1;
        return;
    }
    edge_clip(e);
    next = 0;
    buf[0] = e[1] >> 8;
    buf[1] = (e[3] >> 8) - buf[0];
    if (buf[1] != 0) {
        edge_steps(e);
        fill_corrected(buf, 0, buf[1], r_4baea4, r_4baea8, r_4baebc);
        next = buf[1];
    }
    if (r_4baec8 == 1 && (n = edge_second(e)) != 0) {
        buf[1] += n;
        edge_steps(e);
        fill_corrected(buf, next, n, r_4baea4, r_4baea8, r_4baebc);
    }
}

/* 0x64f0c0: an edge of x alone, clipped. */
static void edge_x_clip(s32 *buf, s32 *e)
{
    int next;
    s32 n;

    if (clip_y0_8() >= e[3]) {
        buf[1] = 0;
        buf[0] = g_gfx_target.clip_y0;
        return;
    }
    if (clip_y1_8() <= e[1]) {
        buf[1] = 0;
        buf[0] = g_gfx_target.clip_y1;
        return;
    }
    edge_clip(e);
    next = 0;
    buf[0] = e[1] >> 8;
    buf[1] = (e[3] >> 8) - buf[0];
    if (buf[1] != 0) {
        edge_steps(e);
        fill_plain(buf, 0, buf[1], r_4baea4, r_4baea8, r_4baebc);
        next = buf[1];
    }
    if (r_4baec8 == 1 && (n = edge_second(e)) != 0) {
        buf[1] += n;
        edge_steps(e);
        fill_plain(buf, next, n, r_4baea4, r_4baea8, r_4baebc);
    }
}

/* 0x467f1c: the spans' step, -du/dx and -dv/dx packed: dv/dx in 8.8 in
 * the low 16 bits, du/dx's fraction in the high 16, its integer apart. */
static void span_setup_467f1c(s32 dvdx, s32 dudx, const u8 *page, const u8 *table)
{
    u32 b = (u32)-dudx;

    r_4bad6c = table;
    r_4bad68 = page;
    b = (b >> 16) | (b << 16);
    r_4bad60 = (u8)b;
    r_4bad64 = (b & 0xffff0000u) | ((u32)-dvdx >> 8 & 0xffff);
}

/* 0x468030: the same for the plain spans. */
static void span_setup_468030(s32 dvdx, s32 dudx, const u8 *page)
{
    span_setup_467f1c(dvdx, dudx, page, NULL);
}

/* 0x45c990 */
static void span_setup_45c990(s32 dvdx, s32 dudx, const u8 *page, const u8 *table)
{
    span_setup_467f1c(dvdx, dudx, page, table);
}

/* 0x45d7e0 */
static void span_setup_45d7e0(s32 dvdx, s32 dudx, const u8 *page)
{
    span_setup_468030(dvdx, dudx, page);
}

/* One step left: the packed register and u's integer byte. */
static u8 texel_step(u32 *c, u8 *u)
{
    u64 s = (u64)*c + r_4bad64;

    *c = (u32)s;
    *u = (u8)(*u + r_4bad60 + (u8)(s >> 32));
    return r_4bad68[(*c & 0xff00) | *u];
}

/* 0x467f42: a span of `count` pixels right to left from dst, through
 * the table, then the pixel left of them, the left edge's texel. */
static void span_467f42(u8 *dst, s32 count, u32 c, s32 u_int, const s32 *left)
{
    u8 u = (u8)left[1], v = (u8)left[0];
    s32 i;

    r_4bad70 = r_4bad68[v << 8 | u];
    u = (u8)u_int;
    for (i = count - 1; i >= 0; i--) {
        u8 t = texel_step(&c, &u);

        dst[i] = r_4bad6c[t << 8 | dst[i]];
    }
    if (count >= 0) {
        dst[-1] = r_4bad6c[r_4bad70 << 8 | dst[-1]];
    }
}

/* 0x467f97: a span cut by the clip's left side. */
static void span_467f97(u8 *dst, s32 count, u32 c, s32 u_int)
{
    u8 u = (u8)u_int;
    s32 i;

    for (i = count - 1; i >= 0; i--) {
        u8 t = texel_step(&c, &u);

        dst[i] = r_4bad6c[t << 8 | dst[i]];
    }
}

/* 0x468050: 0x467f42 with the texels as they are (the original writes
 * them four at a time). */
static void span_468050(u8 *dst, s32 count, u32 c, s32 u_int, const s32 *left)
{
    u8 u = (u8)left[1], v = (u8)left[0], l;
    s32 i;

    l = r_4bad68[v << 8 | u];
    u = (u8)u_int;
    for (i = count - 1; i >= 0; i--) {
        dst[i] = texel_step(&c, &u);
    }
    if (count >= 0) {
        dst[-1] = l;
    }
}

/* 0x4680c6: 0x467f97 with the texels as they are. */
static void span_4680c6(u8 *dst, s32 count, u32 c, s32 u_int)
{
    u8 u = (u8)u_int;
    s32 i;

    for (i = count - 1; i >= 0; i--) {
        dst[i] = texel_step(&c, &u);
    }
}

/* 0x45c9b0 */
static void span_45c9b0(u8 *dst, s32 count, u32 c, s32 u_int, const s32 *left)
{
    span_467f42(dst, count, c, u_int, left);
}

/* 0x45c9d0 */
static void span_45c9d0(u8 *dst, s32 count, u32 c, s32 u_int)
{
    span_467f97(dst, count, c, u_int);
}

/* 0x45d800 */
static void span_45d800(u8 *dst, s32 count, u32 c, s32 u_int, const s32 *left)
{
    span_468050(dst, count, c, u_int, left);
}

/* 0x45d820 */
static void span_45d820(u8 *dst, s32 count, u32 c, s32 u_int)
{
    span_4680c6(dst, count, c, u_int);
}

/* 0x45d7a0: an edge's dx/dy in 8.8, or the widest either way for a
 * level one. */
static s32 slope_45d7a0(s32 x0, s32 y0, s32 x1, s32 y1)
{
    if (y1 == y0) {
        return x1 < x0 ? (s32)0x80010000u : 0x7fff0000;
    }
    return (s32)idiv64((s32)((u32)(x0 - x1) << 8), y0 - y1);
}

/* 0x45e5d0: the same for 0x45d8a0. */
static s32 slope_45e5d0(s32 x0, s32 y0, s32 x1, s32 y1)
{
    if (y1 == y0) {
        return x0 > x1 ? (s32)0x80010000u : 0x7fff0000;
    }
    return (s32)idiv64((s32)((u32)(x0 - x1) << 8), y0 - y1);
}

/* The rows between two edge buffers: the left's entries from `li`,
 * the right's (which carry u and v) from `ri`. With `clipped`, a row
 * whose left end is left of the clip starts at the clip. */
static void draw_rows(const s32 *left, int li, const s32 *right, int ri, s32 n, int clipped,
                      int copy)
{
    s32 k;

    for (k = 0; k < n; k++) {
        const s32 *l = left + 2 + 3 * (li + k), *r = right + 2 + 3 * (ri + k);
        u32 c = (u32)(r[0] & 0xffff) + ((u32)r[1] << 24);

        if (li + k >= EDGE_ROWS || ri + k >= EDGE_ROWS) {
            break;
        }
        if (clipped && g_gfx_target.clip_x0 > l[2]) {
            u8 *dst = r_51a9d0 + g_gfx_target.clip_x0;
            s32 count = r[2] - g_gfx_target.clip_x0;

            if (copy) {
                span_45d820(dst, count, c, r[1] >> 8);
            } else {
                span_45c9d0(dst, count, c, r[1] >> 8);
            }
        } else if (copy) {
            span_45d800(r_51a9d0 + l[2] + 1, r[2] - l[2] - 1, c, r[1] >> 8, l);
        } else {
            span_45c9b0(r_51a9d0 + l[2] + 1, r[2] - l[2] - 1, c, r[1] >> 8, l);
        }
        r_51a9d0 += g_gfx_target.pitch;
    }
}

static void set_edge(int a, int b)
{
    const s32 *t = r_4bad74;

    s_edge[0] = t[2 * a];
    s_edge[1] = t[2 * a + 1];
    s_edge[2] = t[2 * b];
    s_edge[3] = t[2 * b + 1];
    s_edge[4] = t[6 + 2 * a];
    s_edge[5] = t[6 + 2 * a + 1];
    s_edge[6] = t[6 + 2 * b];
    s_edge[7] = t[6 + 2 * b + 1];
}

/* 0x45d170 (copy 0) and 0x45dfb0 (copy 1): the sorted triangle, partly
 * off the clip. */
static void triangle_clipped(int copy, const u8 *table)
{
    const s32 *t = r_4bad74;
    s32 *d8 = copy ? r_51aa5c : r_51a9d8, *dc = copy ? r_51aa60 : r_51a9dc;
    s32 *e0 = copy ? r_51aa64 : r_51a9e0;
    s32 s12, s01;

    if (t[5] < clip_y0_8() || t[1] > clip_y1_8() || r_51a9d4 > clip_x1_8()
        || r_51a9f8 < clip_x0_8()) {
        return;
    }
    gradients();
    if (copy) {
        span_setup_45d7e0(r_4bada8, r_4bada4, r_51a9c4);
        s12 = slope_45e5d0(t[2], t[3], t[4], t[5]);
        s01 = slope_45e5d0(t[0], t[1], t[2], t[3]);
    } else {
        span_setup_45c990(r_4bada8, r_4bada4, r_51a9c4, table);
        s12 = slope_45d7a0(t[2], t[3], t[4], t[5]);
        s01 = slope_45d7a0(t[0], t[1], t[2], t[3]);
    }
    if (s12 > s01) {
        set_edge(0, 1);
        edge_x_clip(d8, s_edge);
        set_edge(0, 2);
        edge_uv_clip(dc, s_edge);
        set_edge(1, 2);
        edge_x_clip(e0, s_edge);
        r_51a9d0 += d8[0] * g_gfx_target.pitch;
        draw_rows(d8, 0, dc, 0, d8[1], 1, copy);
        draw_rows(e0, 0, dc, d8[1] > 0 ? d8[1] : 0, e0[1], 1, copy);
    } else {
        set_edge(0, 1);
        edge_uv_clip(d8, s_edge);
        set_edge(0, 2);
        edge_x_clip(dc, s_edge);
        set_edge(1, 2);
        edge_uv_clip(e0, s_edge);
        r_51a9d0 += dc[0] * g_gfx_target.pitch;
        draw_rows(dc, 0, d8, 0, d8[1], 1, copy);
        draw_rows(dc, d8[1] > 0 ? d8[1] : 0, e0, 0, e0[1], 1, copy);
    }
}

/* 0x45d170 */
static void triangle_clipped_45d170(const u8 *table)
{
    triangle_clipped(0, table);
}

/* 0x45dfb0 */
static void triangle_clipped_45dfb0(void)
{
    triangle_clipped(1, NULL);
}

static void swap(int a, int b)
{
    s32 *t = r_4bad74, k;

    k = t[2 * a], t[2 * a] = t[2 * b], t[2 * b] = k;
    k = t[2 * a + 1], t[2 * a + 1] = t[2 * b + 1], t[2 * b + 1] = k;
    k = t[6 + 2 * a], t[6 + 2 * a] = t[6 + 2 * b], t[6 + 2 * b] = k;
    k = t[7 + 2 * a], t[7 + 2 * a] = t[7 + 2 * b], t[7 + 2 * b] = k;
}

/* The body of 0x45ca50 and 0x45d8a0: sorts the triangle by y, and
 * draws it here when it is within the clip, else by 0x45d170. */
static void triangle(const struct turned_tri *tri, int copy)
{
    s32 *t = r_4bad74;
    s32 *d8 = copy ? r_51aa5c : r_51a9d8, *dc = copy ? r_51aa60 : r_51a9dc;
    s32 *e0 = copy ? r_51aa64 : r_51a9e0;
    s32 s02, s01;

    if (d8 == NULL) {
        return;
    }
    t[0] = tri->x0, t[1] = tri->y0, t[2] = tri->x1, t[3] = tri->y1;
    t[4] = tri->x2, t[5] = tri->y2;
    t[6] = tri->u0, t[7] = tri->v0, t[8] = tri->u1, t[9] = tri->v1;
    t[10] = tri->u2, t[11] = tri->v2;
    r_51a9c4 = tri->page;
    r_51a9d0 = g_gfx_target.base;
    r_51a9d4 = t[0] < t[2] ? t[0] : t[2];
    r_51a9f8 = t[0] < t[2] ? t[2] : t[0];
    if (t[4] > r_51a9f8) {
        r_51a9f8 = t[4];
    } else if (r_51a9d4 > t[4]) {
        r_51a9d4 = t[4];
    }
    if (t[1] > t[5]) {
        swap(0, 2);
    }
    if (t[1] > t[3]) {
        swap(0, 1);
    } else if (t[5] < t[3]) {
        swap(1, 2);
    }
    if (t[1] < clip_y0_8() || t[5] > clip_y1_8() || r_51a9f8 > clip_x1_8()
        || r_51a9d4 < clip_x0_8()) {
        if (copy) {
            triangle_clipped_45dfb0();
        } else {
            triangle_clipped_45d170(tri->table);
        }
        return;
    }
    gradients();
    if (copy) {
        span_setup_45d7e0(r_4bada8, r_4bada4, r_51a9c4);
        s02 = slope_45e5d0(t[0], t[1], t[4], t[5]);
        s01 = slope_45e5d0(t[0], t[1], t[2], t[3]);
    } else {
        span_setup_45c990(r_4bada8, r_4bada4, r_51a9c4, tri->table);
        s02 = slope_45d7a0(t[0], t[1], t[4], t[5]);
        s01 = slope_45d7a0(t[0], t[1], t[2], t[3]);
    }
    if (s02 > s01) {
        set_edge(0, 1);
        edge_x(d8, s_edge);
        set_edge(0, 2);
        edge_uv(dc, s_edge);
        set_edge(1, 2);
        edge_x(e0, s_edge);
        r_51a9d0 += d8[0] * g_gfx_target.pitch;
        draw_rows(d8, 0, dc, 0, d8[1], 0, copy);
        draw_rows(e0, 0, dc, d8[1] > 0 ? d8[1] : 0, e0[1], 0, copy);
    } else {
        set_edge(0, 1);
        edge_uv(d8, s_edge);
        set_edge(0, 2);
        edge_x(dc, s_edge);
        set_edge(1, 2);
        edge_uv(e0, s_edge);
        r_51a9d0 += dc[0] * g_gfx_target.pitch;
        draw_rows(dc, 0, d8, 0, d8[1], 0, copy);
        draw_rows(dc, d8[1] > 0 ? d8[1] : 0, e0, 0, e0[1], 0, copy);
    }
}

/* 0x45ca50 */
void turned_triangle(const struct turned_tri *t)
{
    triangle(t, 0);
}

/* 0x45d8a0 */
void turned_triangle_copy(const struct turned_tri *t)
{
    triangle(t, 1);
}

/* 0x468c70
 * The sprite's four corners, turned by m about the hot spot (each
 * product's bits 16..47), its box 1/16 texel in from the right and
 * bottom; drawn as the triangles (0, 1, 2) and (1, 2, 3). */
void turned_sprite(const struct turned_src *s, const s32 *m, s32 x, s32 y)
{
    static struct turned_tri tri;       /* 0x4baf54 */
    s32 v0 = s->v0, v1 = s->v1 - 0x10, u0 = s->u0, u1 = s->u1 - 0x10;
    s32 ax, ay, bx, by, cx, cy, dx, dy, w, h;

    ax = mul_mid(-s->hot_x, m[0]) + mul_mid(-s->hot_y, m[2]);
    ay = mul_mid(-s->hot_x, m[1]) + mul_mid(-s->hot_y, m[3]);
    w = u1 - u0;
    by = mul_mid(m[1], w);
    bx = mul_mid(m[0], w);
    h = v1 - v0;
    cy = mul_mid(m[3], h) + ay;
    dy = cy + by;
    by += ay;
    cx = mul_mid(m[2], h) + ax;
    dx = cx + bx;
    bx += ax;
    tri.x0 = ax + x;
    tri.y0 = ay + y;
    tri.x1 = bx + x;
    tri.y1 = by + y;
    tri.x2 = cx + x;
    tri.y2 = cy + y;
    tri.v0 = v0;
    tri.v1 = v0;
    tri.v2 = v1;
    tri.u0 = u0;
    tri.u2 = u0;
    tri.u1 = u1;
    tri.page = s->page;
    tri.table = s->table;
    turned_triangle(&tri);
    tri.x0 = bx + x;
    tri.y0 = by + y;
    tri.x1 = cx + x;
    tri.y1 = cy + y;
    tri.x2 = dx + x;
    tri.y2 = dy + y;
    tri.v0 = v0;
    tri.v1 = v1;
    tri.v2 = v1;
    tri.u0 = u1;
    tri.u2 = u1;
    tri.u1 = u0;
    tri.page = s->page;
    tri.table = s->table;
    turned_triangle(&tri);
}
