/*
 * soft3d/persp.c - the perspective correct textured triangle, command
 * types 20 and 21 (0x4516f0, 0x4512e0, 0x450f90, 0x451470, 0x4537dc).
 *
 * The triangle is split at its middle vertex into two trapezoids. Down
 * each, a row's ends are found on the long edge and the short edge, and
 * the texture there by the depth along each edge; a row is drawn from
 * its right end to its left in runs of 16 pixels, the texture found
 * exactly at each run's end and stepped linearly between. Type 21 puts
 * each texel through a table, table[texel << 8 | dst].
 *
 * The texture coordinates are 8.8 texels: a span packs U and V into one
 * word, U in the high 16 bits, and steps both with one add, so a carry
 * out of V moves U, as the original does.
 *
 * The original runs 0x4537dc with the FPU at single precision, so its
 * arithmetic is done here in float (R() rounds each result); fistp
 * rounds to nearest. After a type 21 row it leaves the FPU at 64 bit
 * precision for the rest of the triangle; this port uses double then.
 */

#include <math.h>
#include <string.h>

#include "soft3d/sw.h"

/* The command (0x4516f0). */
static s32 r_4b63cc;                    /* type 21 */
static const u8 *r_4b63c8;              /* its table */
static const u8 *r_4b63c4;              /* the texture page */

/* The vertices sorted by y (0x4512e0): top, middle, bottom, each x, y
 * (8.8), z, u, v; then x and y in pixels. */
static s32 r_63b580, r_63b584, r_63b588, r_63b58c, r_63b590;
static s32 r_63b594, r_63b598, r_63b59c, r_63b5a0, r_63b5a4;
static s32 r_63b5a8, r_63b5ac, r_63b5b0, r_63b5b4, r_63b5b8;
static s32 r_63b5bc, r_63b5c0, r_63b5c4, r_63b5c8, r_63b5cc, r_63b5d0;
static s32 r_4b63d0, r_4b63d4, r_4b63d8, r_4b63dc, r_4b63e0;  /* the top's */

/* The edges (0x450f90). */
static s32 r_4b63a8, r_4b63ac, r_4b63b8;    /* u, v, z bottom - top */
static s32 r_4b63b0, r_4b63b4, r_4b63bc;    /* the short edge's u, v, z */
static s32 r_63b570, r_63b574, r_63b558;    /* u, v, z bottom - middle */
static s32 r_63b55c, r_63b560;              /* the halves' heights, 8.8 */
static s32 r_63b568, r_63b56c;              /* and in rows */
static float f_4b63a0, f_4b639c, f_4b6398, f_4b63a4, f_63b5e0, f_63b5dc;
static float f_4b6390, f_4b6364;            /* the long edge: dx / dy, length / dy */
static float f_4b6394, f_4b6370;            /* the short edge's */
static float f_63b5d4, f_63b5d8;            /* the bottom edge's */
static s32 r_4b63c0;                        /* the long edge is on the right */

/* The trapezoid (0x451470, 0x4537dc). */
static s32 r_63b578, r_63b57c;          /* rows cut at the top and bottom, 8.8 */
static s32 r_4b6358;                    /* the trapezoid's height, 8.8 */
static s32 r_4b635c, r_4b6368;          /* the row down the long and short edges */
static float f_4b6360, f_4b636c;        /* and along them */
static s32 r_4b6374, r_4b6378, r_4b637c, r_4b6380, r_4b6384;  /* the short edge's start */

/* The row and its span (0x4537dc). */
static u8 *r_4b63f0;                    /* the row */
static s32 r_4b63fc, r_4b6428;          /* right and left x, 8.8 */
static s32 r_4b6320, r_4b6324, r_4b6418;    /* u, v, z at the long end, then the right */
static s32 r_4b63e4, r_4b63e8, r_4b641c;    /* and at the short end, then the left */
static s32 r_4b6430;                    /* the row's width, 8.8 */
static s32 r_4b6388, r_4b638c;          /* u, v at the right end */
static s32 r_4b6350, r_4b6354;          /* left - right */
static s32 r_4b643c;
static s32 r_4b6414, r_4b6410;          /* the span's length and start, 8.8 */
static float f_4b6434, f_4b6438;        /* r * width, 1 - r: r the ends' depths' ratio */
static s32 r_4b6404;                    /* the span's pixels */
static u32 r_4b63ec;                    /* u << 16 | v at the first pixel */
static const u8 *r_4b63f8;
static s32 r_4b634c, r_4b6314, r_4b6310;
static u32 r_4b631c;

/* The hook's tables (0x453c33). */
static float f_4b6448[17];              /* 1 / k */
static s32 r_4b6498[17];
static float f_4b6318;                  /* 4096: 16 pixels in 8.8 */
static const s32 r_4b6330 = 0xc00000;   /* 1.5 * 2^23, fild as an int */

/* The FPU's precision: 0 single (0x4537dc's own), 1 extended. */
static int s_ext;

static double R(double x)
{
    return s_ext ? x : (double)(float)x;
}

/* fistp, rounding to nearest; out of range gives the integer
 * indefinite. */
static s32 fist(double x)
{
    double r = rint(x);

    if (!(r >= -2147483648.0 && r < 2147483648.0)) {
        return INT32_MIN;
    }
    return (s32)r;
}

/* x + 1.5 * 2^23 stored as a float and read as an int: the integer
 * nearest x in the low bits, under 0x4b400000. */
static s32 magic(double x)
{
    float s = (float)(x + (double)r_4b6330);
    u32 b;

    memcpy(&b, &s, sizeof(b));
    return (s32)b;
}

/* 0x453c33 */
void sw_hook_453c33(int w, int h)
{
    int k, n = 0;

    (void)w;
    (void)h;
    for (k = 16; k > 0; k--, n++) {
        r_4b6498[k] = 15 * n;
        f_4b6448[k] = (float)(1.0 / k);
    }
    f_4b6448[0] = 0.0f;
    r_4b6498[0] = 0;
    f_4b6318 = 4096.0f;
}

/* 0x4512e0
 * Sorts the vertices by y: the top the first with the least y, the
 * bottom the first with the most, the middle the other. */
static int sort(const struct sw_cmd_persp *c)
{
    int top = 1, bot = 1, mid, k;

    for (k = 2; k < 4; k++) {
        if (c->v[top - 1].y > c->v[k - 1].y) {
            top = k;
        }
        if (c->v[bot - 1].y < c->v[k - 1].y) {
            bot = k;
        }
    }
    mid = top + bot == 3 ? 3 : top > bot ? top - bot : bot - top;
    /* All three on one row: the original reads before the command. The
     * face handlers leave out such a face. */
    if (mid == 0) {
        return 0;
    }
    top--, mid--, bot--;
    r_63b580 = c->v[top].x;
    r_63b594 = c->v[mid].x;
    r_63b5a8 = c->v[bot].x;
    r_63b584 = c->v[top].y;
    r_63b598 = c->v[mid].y;
    r_63b5ac = c->v[bot].y;
    r_63b588 = c->v[top].z;
    r_63b59c = c->v[mid].z;
    r_63b5b0 = c->v[bot].z;
    r_63b58c = c->uv[2 * top];
    r_63b5a0 = c->uv[2 * mid];
    r_63b5b4 = c->uv[2 * bot];
    r_63b590 = c->uv[2 * top + 1];
    r_63b5a4 = c->uv[2 * mid + 1];
    r_63b5b8 = c->uv[2 * bot + 1];
    r_63b5bc = r_63b580 >> 8;
    r_63b5c0 = r_63b594 >> 8;
    r_63b5c4 = r_63b5a8 >> 8;
    r_63b5c8 = r_63b584 >> 8;
    r_63b5cc = r_63b598 >> 8;
    r_63b5d0 = r_63b5ac >> 8;
    r_4b63d0 = r_63b580;
    r_4b63d4 = r_63b584;
    r_4b63d8 = r_63b588;
    r_4b63dc = r_63b58c;
    r_4b63e0 = r_63b590;
    return 1;
}

static s32 iabs(s32 a)
{
    return a < 0 ? (s32)(0u - (u32)a) : a;
}

static s32 longer(s32 dx, s32 dy)
{
    s32 a = iabs(dy), b = iabs(dx);

    return a > b ? a : b;
}

/* 0x450f90
 * The edges: each one's differences, its length (the larger of its
 * width and height), its slope and length per row, and the terms of
 * the depth along it. */
static void edges(void)
{
    double iz0 = 1.0 / r_63b588, iz1, t, l02, l01, l12;
    s32 dy02, dx02, dy01, dx01, dx12, dy12;

    r_4b63a8 = r_63b5b4 - r_63b58c;
    r_4b63b8 = r_63b5b0 - r_63b588;
    r_4b63ac = r_63b5b8 - r_63b590;
    r_4b63b0 = r_63b5a0 - r_63b58c;
    r_4b63bc = r_63b59c - r_63b588;
    r_4b63b4 = r_63b5a4 - r_63b590;
    r_63b570 = r_63b5b4 - r_63b5a0;
    r_63b574 = r_63b5b8 - r_63b5a4;
    r_63b558 = r_63b5b0 - r_63b59c;
    dy02 = r_63b5ac - r_63b584;
    dx02 = r_63b5a8 - r_63b580;
    l02 = longer(dx02, dy02);
    dy01 = r_63b598 - r_63b584;
    dx01 = r_63b594 - r_63b580;
    l01 = longer(dx01, dy01);
    dx12 = r_63b5a8 - r_63b594;
    dy12 = r_63b5ac - r_63b598;
    l12 = longer(dx12, dy12);
    iz1 = 1.0 / r_63b59c;
    f_4b6390 = 0.0f;
    r_63b55c = dy01;
    r_63b560 = dy12;
    f_63b5d4 = 0.0f;
    r_63b568 = r_63b5cc - r_63b5c8;
    f_4b6394 = 0.0f;
    r_4b63c0 = 0;
    r_63b56c = r_63b5d0 - ((r_63b598 - 1) >> 8);
    f_4b63a0 = (float)(l01 * iz0 * r_63b59c);
    f_4b639c = (float)((double)(r_63b588 - r_63b5b0) * iz0);
    f_4b6398 = (float)(iz0 * l02 * r_63b5b0);
    f_4b63a4 = (float)((double)(r_63b588 - r_63b59c) * iz0);
    f_63b5e0 = (float)(l12 * iz1 * r_63b5b0);
    f_63b5dc = (float)((double)(r_63b59c - r_63b5b0) * iz1);
    if (r_63b5ac != r_63b584) {
        t = 1.0 / dy02;
        f_4b6390 = (float)(dx02 * t);
        f_4b6364 = (float)(t * l02);
    }
    if (r_63b598 != r_63b584) {
        t = 1.0 / dy01;
        f_4b6394 = (float)(dx01 * t);
        f_4b6370 = (float)(t * l01);
    }
    if (r_63b5ac != r_63b598) {
        t = 1.0 / dy12;
        f_63b5d4 = (float)(dx12 * t);
        f_63b5d8 = (float)(t * l12);
    }
    if (!(f_4b6394 > f_4b6390)) {
        r_4b63c0 = 1;
    }
    if (dy01 == 0) {
        r_4b63c0 = r_63b594 < r_63b580;
    }
}

/* 0x4540a4
 * The last pixels of a type 20 span, right to left from p: in fours,
 * then one by one. U's whole part steps in bl with the carry out of
 * its fraction in e's top byte; V is e's low 16 bits. */
static u8 *tail_plain(u8 *p, int groups, int singles, u32 e, u32 step, u8 u, u8 v, u8 du)
{
    const u8 *tex = r_4b63f8;
    int k;

    for (; groups > 0; groups--) {
        for (k = 0; k < 4; k++) {
            u32 n = e + step;

            p[-k] = tex[v << 8 | u];
            u = (u8)(u + du + (n < e));
            e = n;
            v = (u8)(e >> 8);
        }
        p -= 4;
    }
    for (; singles > 0; singles--) {
        u32 n = e + step;

        *p-- = tex[v << 8 | u];
        u = (u8)(u + du + (n < e));
        e = n;
        v = (u8)(e >> 8);
    }
    return p;
}

/* 0x45444c: the last pixels of a type 21 span, from p + count down to
 * p + 1, each table[texel << 8 | dst]. */
static void tail_table(u8 *p, int count, u32 e, u32 step)
{
    for (; count > 0; count--) {
        u8 t = r_4b63f8[(e >> 24) | (e & 0xff00)];

        p[count] = r_4b63c8[t << 8 | p[count]];
        e += step;
    }
}

/* The fraction of the span at d (8.8) from its right end: the texture
 * moves with the depth across the row. */
static double at(double d)
{
    return R(d / R(R(d * f_4b6438) + f_4b6434));
}

/* A run of 16 pixels right to left from p: the step to the run's end
 * (ebx, ecx, the texture there) from cur, V stepped a pixel ahead. */
static u32 run_step(u32 cur, s32 ebx, s32 ecx, u32 *e)
{
    s32 us = (ebx - (s32)(cur >> 16)) >> 4;
    s32 vs = (ecx - (s32)(cur & 0xffff)) >> 4;
    u32 step = (u32)us << 16 | ((u32)vs & 0xffff);

    *e = (cur & 0xffff0000u) | ((cur - ((u32)vs & 0xffff)) & 0xffff);
    return step;
}

/* 0x453fc3: a type 20 span of 16 pixels or fewer, from p. */
static void short_plain(u8 *p, int count)
{
    double fe = at((double)r_4b6414), x, y;
    s32 bx, by;
    u32 e;

    r_4b6350 = fist(R(r_4b6350 * fe));
    r_4b6354 = fist(R(r_4b6354 * fe));
    r_4b6350 += r_4b6388 - r_4b6320;
    r_4b6354 += r_4b638c - r_4b6324;
    x = R(r_4b6350 * (double)f_4b6448[count]);
    y = R(r_4b6354 * (double)f_4b6448[count]);
    bx = fist(x);
    by = fist(y);
    e = ((r_4b63ec << 8) & 0xffff0000u) | (r_4b63ec & 0xffff);
    e &= 0xff00ffffu;
    tail_plain(p, count >> 2, count & 3, e, (u32)bx << 24 | ((u32)by & 0xffff),
               (u8)(r_4b63ec >> 24), (u8)(r_4b63ec >> 8), (u8)(bx >> 8));
    s_ext = 0;
}

/* 0x453cc8: a type 20 span, right to left from the row's pixel
 * r_4b63fc. */
static void span_plain(void)
{
    u8 *p = r_4b63f0 + ((u32)r_4b63fc >> 8);
    int count = r_4b6404, n;
    double du, dv, d, f;
    s32 ebx, ecx;
    u32 edx;

    if (count <= 16) {
        short_plain(p, count);
        return;
    }
    dv = r_4b6354;
    du = r_4b6350;
    d = R(r_4b6410 + (double)f_4b6318);
    n = count - 1;
    f = at(d);
    r_4b634c = (n & 15) + 1;
    r_4b6314 = n >> 4;
    ebx = fist(R(f * du)) + r_4b6388;
    ecx = fist(R(f * dv)) + r_4b638c;
    r_4b6310 = r_4b6414 - ((r_4b6314 << 12) + r_4b6410);
    edx = r_4b63ec;
    /* p is the right pixel; the original steps its pointer one past. */
    for (;;) {
        u32 e, step;
        int k;

        if (--r_4b6314 > 0) {
            d = R(d + (double)f_4b6318);
        } else if (r_4b6314 == 0) {
            d = R(d + (double)r_4b6310);
        } else {
            break;
        }
        r_4b631c = (u32)ebx << 16 | ((u32)ecx & 0xffff);
        step = run_step(edx, ebx, ecx, &e);
        f = at(d);
        for (k = 0; k < 16; k++) {
            u32 nx = e + step;

            p[-k] = r_4b63f8[(e >> 24) | (nx & 0xff00)];
            e = nx;
        }
        ebx = magic(R(f * du)) + r_4b6388;
        ecx = magic(R(f * dv)) + r_4b638c;
        edx = r_4b631c;
        p -= 16;
    }
    /* 0x453f0e: the rest, 1 to 16 pixels, stepped linearly to the left
     * end. */
    {
        int rest = r_4b634c;
        double x, y;
        s32 bx, by;
        u32 e;

        r_4b63ec = edx;
        r_4b6350 = (ebx & 0xffff) - (s32)(edx >> 16);
        r_4b6354 = (ecx & 0xffff) - (s32)(edx & 0xffff);
        x = R(r_4b6350 * (double)f_4b6448[rest]);
        y = R(r_4b6354 * (double)f_4b6448[rest]);
        bx = magic(x);
        by = magic(y);
        e = ((r_4b63ec << 8) & 0xffff0000u) | (r_4b63ec & 0xffff);
        tail_plain(p, rest >> 2, rest & 3, e, (u32)bx << 24 | ((u32)by & 0xffff),
                   (u8)(r_4b63ec >> 24), (u8)(r_4b63ec >> 8), (u8)(bx >> 8));
    }
    s_ext = 0;
}

/* 0x454473: a type 21 span of 16 pixels or fewer, from p. */
static void short_table(u8 *p, int count)
{
    double fe;
    s32 x, y;

    if (count == 0) {
        s_ext = 1;
        return;
    }
    fe = at((double)r_4b6414);
    r_4b6350 = fist(R(r_4b6350 * fe));
    r_4b6354 = fist(R(r_4b6354 * fe));
    r_4b6350 += r_4b6388 - r_4b6320;
    r_4b6354 += r_4b638c - r_4b6324;
    x = fist(R(r_4b6350 * (double)f_4b6448[count]));
    y = fist(R(r_4b6354 * (double)f_4b6448[count]));
    tail_table(p - count, count, r_4b63ec, (u32)x << 16 | ((u32)y & 0xffff));
    s_ext = 1;
}

/* 0x454104: a type 21 span, as 0x453cc8 but through the table. */
static void span_table(void)
{
    u8 *p = r_4b63f0 + ((u32)r_4b63fc >> 8);
    int count = r_4b6404, n;
    double du, dv, d, f;
    s32 ebx, ecx;
    u32 edx;

    if (count <= 16) {
        short_table(p, count);
        return;
    }
    dv = r_4b6354;
    du = r_4b6350;
    d = R(r_4b6410 + (double)f_4b6318);
    n = count - 1;
    f = at(d);
    r_4b634c = (n & 15) + 1;
    r_4b6314 = n >> 4;
    ebx = fist(R(f * du)) + r_4b6388;
    ecx = fist(R(f * dv)) + r_4b638c;
    r_4b6310 = r_4b6414 - ((r_4b6314 << 12) + r_4b6410);
    edx = r_4b63ec;
    for (;;) {
        u32 e, step;
        int k;

        if (--r_4b6314 > 0) {
            d = R(d + (double)f_4b6318);
        } else if (r_4b6314 == 0) {
            d = R(d + (double)r_4b6310);
        } else {
            break;
        }
        r_4b631c = (u32)ebx << 16 | ((u32)ecx & 0xffff);
        step = run_step(edx, ebx, ecx, &e);
        f = at(d);
        for (k = 0; k < 16; k++) {
            u32 nx = e + step;
            u8 t = r_4b63f8[(e >> 24) | (nx & 0xff00)];

            p[-k] = r_4b63c8[t << 8 | p[-k]];
            e = nx;
        }
        ebx = magic(R(f * du)) + r_4b6388;
        ecx = magic(R(f * dv)) + r_4b638c;
        edx = r_4b631c;
        p -= 16;
    }
    /* 0x4543c2 */
    {
        int rest = r_4b634c;
        s32 x, y;

        r_4b63ec = edx;
        r_4b6350 = (ebx & 0xffff) - (s32)(edx >> 16);
        r_4b6354 = (ecx & 0xffff) - (s32)(edx & 0xffff);
        if (rest != 0) {
            x = fist(R(r_4b6350 * (double)f_4b6448[rest]));
            y = fist(R(r_4b6354 * (double)f_4b6448[rest]));
            tail_table(p - rest, rest, r_4b63ec, (u32)x << 16 | ((u32)y & 0xffff));
        }
    }
    s_ext = 1;
}

/* 0x453a0d
 * A row's span from its ends: the right end's texture, the ratio of the
 * ends' depths for the steps between, the span cut to the clip. */
static void row(void)
{
    double r, f0, dd;
    s32 t, xr, xl, count;

    if (r_4b63c0 == 0) {
        t = r_4b6418, r_4b6418 = r_4b641c, r_4b641c = t;
        t = r_4b63fc, r_4b63fc = r_4b6428, r_4b6428 = t;
        t = r_4b6320, r_4b6320 = r_4b63e4, r_4b63e4 = t;
        t = r_4b6324, r_4b6324 = r_4b63e8, r_4b63e8 = t;
    }
    r_4b6430 = r_4b63fc - r_4b6428;
    r_4b6388 = r_4b6320;
    r_4b638c = r_4b6324;
    r = R((double)r_4b641c / r_4b6418);
    r_4b6350 = r_4b63e4 - r_4b6320;
    r_4b6354 = r_4b63e8 - r_4b6324;
    r_4b643c = r_4b641c - r_4b6418;
    xr = r_4b63fc;
    xl = r_4b6428;
    if (xl <= g_sw_clip[0]) {
        xl = g_sw_clip[0] + 0x10;
    }
    r_4b6428 = xl;
    xl = (xl & ~0xff) + 0x100;
    r_4b6414 = xr - xl;
    f_4b6434 = (float)R(r * r_4b6430);
    f_4b6438 = (float)R(1.0 - r);
    r_4b6410 = xr & 0xff;
    if (xr >= g_sw_clip[2]) {
        r_4b6410 = xr - (g_sw_clip[2] & ~0xff);
        r_4b63fc = g_sw_clip[2];
    }
    dd = r_4b6410;
    f0 = R(dd / R(R(dd * f_4b6438) + f_4b6434));
    count = (s32)((u32)r_4b63fc >> 8) - (s32)((u32)r_4b6428 >> 8);
    if (count <= 0) {
        return;
    }
    r_4b6404 = count;
    r_4b63fc = (r_4b63fc & ~0xff) - 0x100;
    r_4b6320 = fist(R(R(r_4b6350 * f0) + r_4b6388));
    r_4b6324 = fist(R(R(r_4b6354 * f0) + r_4b638c));
    r_4b63ec = (u32)r_4b6320 << 16 | ((u32)r_4b6324 & 0xffff);
    r_4b63f8 = r_4b63c4;
    if (r_4b63fc > g_sw_clip[2] || r_4b6404 > g_sw_pitch) {
        return;
    }
    if (r_4b63cc & 1) {
        span_table();
    } else {
        span_plain();
    }
}

/* u, v, z along an edge at w of the way: start + delta * w. */
static void along(double w, s32 du, s32 dv, s32 dz, s32 u, s32 v, s32 z, s32 *ou, s32 *ov,
                  s32 *oz)
{
    *ou = fist(R(R(du * w) + u));
    *ov = fist(R(R(dv * w) + v));
    *oz = fist(R(R(dz * w) + z));
}

/* 0x4537dc
 * The rows of a trapezoid, while the short edge's row r_4b6368 is no
 * more than r_4b6358; each row's ends are where its bottom boundary
 * crosses the edges. */
static void trapezoid(void)
{
    s_ext = 0;
    if (r_4b6368 > r_4b6358) {
        s_ext = 1;
        return;
    }
    r_4b63f0 = g_sw_dst + (size_t)(((u32)(r_4b635c + r_4b63d4) >> 8) * (u32)g_sw_pitch)
               - g_sw_pitch;
    for (;;) {
        double tl = f_4b6360, wl, ws;
        double xl = R(R((double)r_4b635c * f_4b6390) + r_4b63d0);
        double xs = R(R((double)r_4b6368 * f_4b6394) + r_4b6374);
        int skip = 0;

        r_4b63fc = fist(xl);
        r_4b6428 = fist(xs);
        wl = R(tl / R(R(tl * f_4b639c) + f_4b6398));
        if (r_4b63c0 == 0) {
            skip = r_4b63fc >= g_sw_clip[2] || g_sw_clip[0] > r_4b6428;
        } else {
            skip = r_4b63fc < g_sw_clip[0] || g_sw_clip[2] <= r_4b6428;
        }
        if (!skip && (r_4b63fc >> 8) - (r_4b6428 >> 8) == 0) {
            skip = 1;
        }
        if (!skip) {
            along(wl, r_4b63a8, r_4b63ac, r_4b63b8, r_4b63dc, r_4b63e0, r_4b63d8, &r_4b6320,
                  &r_4b6324, &r_4b6418);
            ws = R(f_4b636c / R(R(f_4b636c * f_4b63a4) + f_4b63a0));
            along(ws, r_4b63b0, r_4b63b4, r_4b63bc, r_4b6380, r_4b6384, r_4b637c, &r_4b63e4,
                  &r_4b63e8, &r_4b641c);
            row();
        }
        /* 0x4539b7 */
        r_4b635c += 0x100;
        r_4b6368 += 0x100;
        f_4b6360 = (float)R((double)r_4b635c * f_4b6364);
        f_4b636c = (float)R((double)r_4b6368 * f_4b6370);
        if (r_4b6368 > r_4b6358) {
            break;
        }
        r_4b63f0 += g_sw_pitch;
    }
    /* finit, and the control word it saved: 64 bit precision. */
    s_ext = 1;
}

/* 0x451470
 * The two trapezoids, above and below the middle vertex, cut to the
 * clip's rows. */
static void halves(void)
{
    s32 edi = g_sw_clip[1] - r_63b584, edx = r_63b5ac - g_sw_clip[3], esi, ecx, ebx, eax;

    r_63b578 = edi;
    if (edi < 0) {
        edi = 0;
    }
    r_63b57c = edx;
    if (edx < 0) {
        edx = 0;
    }
    esi = r_63b55c;
    ecx = r_63b560;
    if (esi > edi && r_63b568 != 0) {
        if (ecx < edx) {
            esi = esi - edx + ecx;
        }
        r_63b578 = edi;
        if (edi == 0) {
            edi = 0x100 - (r_63b584 & 0xff);
        } else {
            edi += 0x100;
        }
        r_63b57c = edx;
        r_63b55c = esi;
        r_4b635c = edi;
        r_4b6368 = edi;
        f_4b636c = (float)((double)edi * f_4b6370);
        f_4b6360 = (float)((double)edi * f_4b6364);
        r_4b6374 = r_63b580;
        r_4b6378 = r_63b584;
        r_4b637c = r_63b588;
        r_4b6358 = esi;
        r_4b6380 = r_63b58c;
        r_4b6384 = r_63b590;
        if (ecx > edx) {
            r_4b6358 = esi - 1;
        }
        trapezoid();
        edx = r_63b57c;
        edi = r_63b578;
        esi = r_63b55c;
        ecx = r_63b560;
    }
    /* 0x4515a0 */
    r_63b57c = edx;
    r_63b578 = edi;
    if (ecx < edx || r_63b56c == 0) {
        return;
    }
    f_4b6370 = f_63b5d8;
    ebx = 0x100 - (r_63b598 & 0xff);
    if (ebx == 0x100) {
        ebx = 0;
    }
    eax = esi + ebx;
    if (esi < edi) {
        ebx = edi - esi + 0x100;
        eax = edi + 0x100;
    }
    r_4b635c = eax;
    f_4b6360 = (float)((double)eax * f_4b6364);
    ecx -= edx;
    r_4b6368 = ebx;
    f_4b636c = (float)((double)ebx * f_63b5d8);
    r_63b560 = ecx;
    r_4b6374 = r_63b594;
    r_4b6378 = r_63b598;
    r_4b637c = r_63b59c;
    r_4b6380 = r_63b5a0;
    f_4b6394 = f_63b5d4;
    r_4b6384 = r_63b5a4;
    r_4b63b0 = r_63b570;
    r_4b63b4 = r_63b574;
    r_4b63bc = r_63b558;
    f_4b63a0 = f_63b5e0;
    f_4b63a4 = f_63b5dc;
    r_63b55c = ecx;
    r_4b6358 = ecx;
    trapezoid();
}

/* 0x4516f0 */
void sw_raster_persp(const struct sw_cmd_persp *c)
{
    r_4b63cc = 0;
    if (c->type == SW_PERSP_TABLE) {
        r_4b63cc = 1;
        r_4b63c8 = c->table;
    }
    r_4b63c4 = c->tex;
    if (!sort(c)) {
        return;
    }
    edges();
    halves();
}
