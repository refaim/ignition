/*
 * soft3d/flat.c - the rasteriser's untextured types: the screen fill,
 * points, lines, flat and gouraud triangles, and flat triangles through
 * a table (cases 0, 1, 2, 11, 12, 15, 16 and 19 of 0x44f0e9).
 *
 * The original's fillers are hand written: the edges are 16.16 values
 * whose integer half the spans read as a 16 bit word, the lines step
 * with 16 bit fractions and carries, and the loops are unrolled. Here
 * each is one loop with the same arithmetic, so the same pixels come
 * out. A colour index is looked up in a 64 KB aligned table whose low
 * 16 bits the original fills with two bytes: index = hi << 8 | lo.
 */

#include <string.h>

#include "soft3d/sw.h"

/* The 16 bit word of an edge's integer half, as the spans read it. */
static s32 edge_int(u32 f)
{
    return (s16)(f >> 16);
}

/* A left edge from x with a run of d pixels over a row step `rcp`
 * (0x10000 / (dy + 1) - 1): its 16.16 start and step. `inc` is whether
 * d takes the rounding up (the original's tests differ, d > 0 or
 * d >= 0, edge by edge). A left edge going left starts a step on, at
 * the end of the pixel. */
static void edge_left(s32 x, s32 d, u32 rcp, int inc, u32 *f, s32 *step)
{
    if (inc) {
        *step = sw_mul(d + 1, (s32)rcp);
        *f = (u32)x << 16;
    } else {
        *step = sw_mul(d - 1, (s32)rcp);
        *f = (((u32)x << 16) | 0xffff) + (u32)*step;
    }
}

/* A right edge: one going right starts a step on. */
static void edge_right(s32 x, s32 d, u32 rcp, int inc, u32 *f, s32 *step)
{
    if (inc) {
        *step = sw_mul(d + 1, (s32)rcp);
        *f = ((u32)x << 16) + (u32)*step;
    } else {
        *step = sw_mul(d - 1, (s32)rcp);
        *f = ((u32)x << 16) | 0xffff;
    }
}

/* The three vertices in order of y, ties as the original's exchanges
 * leave them (0x450693, 0x452f38, 0x454604): `y` the 3 keys. */
static void sort3(const s32 *y, int *a, int *b, int *c)
{
    int t0 = 0, t1 = 1, t2 = 2, t;

    if (y[t1] < y[t0]) {
        /* 0x4506b7 */
        t = t0, t0 = t1, t1 = t;
        if (!(y[t2] > y[t1])) {
            t = t1, t1 = t2, t2 = t;
            if (!(y[t1] > y[t0])) {
                t = t0, t0 = t1, t1 = t;
            }
        }
    } else if (!(y[t1] < y[t2])) {
        t = t1, t1 = t2, t2 = t;
        if (!(y[t0] < y[t1])) {
            t = t0, t0 = t1, t1 = t;
        }
    }
    *a = t0;
    *b = t1;
    *c = t2;
}

/* The clip's columns as the spans compare them, in 16 bits. */
static int span_clip(s32 *xl, s32 *xr)
{
    s16 x0 = (s16)r_49c9fc, x1 = (s16)r_49ca04;

    if ((s16)*xl < x0) {
        if ((s16)*xr < x0) {
            return 0;
        }
        *xl = r_49c9fc;
        if ((s16)*xr > x1) {
            *xr = r_49ca04;
        }
    } else if ((s16)*xr > x1) {
        if ((s16)*xl > x1) {
            return 0;
        }
        *xr = r_49ca04;
    }
    return 1;
}

/* 0x454548
 * Type 0: fills the clip rectangle with one colour, table[x << 8 | y >> 8]
 * of the command's words (the index's upper bytes from y's). The rows
 * go 16 bytes at a time, then the rest. */
void sw_raster_point(const struct sw_cmd_point *c)
{
    s32 rows = r_49ca08 - r_49ca00 + 1, w = r_49ca04 - r_49c9fc + 1, skip = g_sw_pitch - w;
    u32 idx = (((u32)c->y >> 8) & 0xffff00ffu) | ((u32)(u8)c->x << 8);
    u8 colour = g_sw_point_table[idx];
    u8 *p = g_sw_dst + sw_mul(r_49ca00, g_sw_pitch) + r_49c9fc;

    /* The original's block loop counts down from w >> 4 and would run
     * on for a clip under 16 wide; the port draws what is there. */
    for (; rows > 0; rows--) {
        memset(p, colour, (size_t)((u32)w >> 4) * 16 + (w & 15));
        p += ((u32)w >> 4) * 16 + (w & 15) + skip;
    }
}

/* 0x450630: type 1, a pixel of table[colour << 8 | f14 >> 8]. */
void sw_raster_point_flat(const struct sw_cmd_point *c)
{
    s32 x = c->x, y = c->y;

    if (x < r_49ca1c || x > r_49ca24 || y < r_49ca20 || y > r_49ca28) {
        return;
    }
    g_sw_dst[g_sw_rows[y >> 8] + (x >> 8)]
        = g_sw_point_table[(u32)(u8)c->colour << 8 | (u8)(c->f14 >> 8)];
}

/* 0x453790: type 2, a pixel through a table, table[colour << 8 | dst]. */
void sw_raster_point_table(const struct sw_cmd_point_table *c)
{
    s32 x = c->x, y = c->y;
    u8 *p;

    if (x < r_49ca1c || x > r_49ca24 || y < r_49ca20 || y > r_49ca28) {
        return;
    }
    p = g_sw_dst + g_sw_rows[y >> 8] + (x >> 8);
    *p = c->table[(u32)(u8)c->colour << 8 | *p];
}

/* The lines of types 11 and 12 (0x451747, 0x4549a3), as 0x44f0e9 draws
 * type 13: clipped by cutting the ends, then a column (steep) or a row
 * at a time with a 16 bit fraction that carries an extra pixel. Type 12
 * runs its colour from c0 to c1 too. */
static s32 r_4afdf0, r_4afdf4, r_4afdf8, r_4afdfc;  /* the ends while clipping */
static s32 r_4afe00;                    /* the share cut, 1.31 */
static s32 r_4b3cd6, r_4b3cda;          /* 0x451730: pitch + 1, 1 - pitch */
static s32 r_4ba41c, r_4ba420;          /* 0x45498c: the same for type 12 */
static s32 r_4afdec, r_4b630c;          /* the reciprocals' address >> 2 */

struct line {
    s32 dc;                             /* 0x4b6548: c1 - c0 */
    s32 c;                              /* 0x4ba42c */
    const u8 *row;                      /* the table's row for type 12 */
    u8 colour;                          /* 0x4afe04: type 11 */
    int shade;                          /* type 12 */
};

static s32 share(s32 k, s32 n)
{
    return (s32)((((u64)(u32)k << 32) / (u32)n) >> 1);
}

static void line_plot(const struct line *l, u8 *p, u16 c)
{
    *p = l->shade ? l->row[c >> 8] : l->colour;
}

static void line_draw(struct line *l, s32 x0, s32 y0, s32 x1, s32 y1)
{
    u8 *p = g_sw_dst + g_sw_rows[y0] + x0;
    s32 cols = x1 - x0 + 1, dy = y1 - y0, rows;
    s32 pitch = dy < 0 ? -g_sw_pitch : g_sw_pitch;
    u16 frac, acc = 0x7fff, c = (u16)l->c, dc, count, step;

    rows = dy < 0 ? -dy + 1 : dy + 1;
    if (cols <= rows) {
        dc = (u16)sw_div(l->dc, rows);
        step = (u16)((u16)rows / (u16)cols);
        frac = (u16)((((u32)((u16)rows % (u16)cols)) << 16) / (u16)cols);
        for (count = (u16)cols; count != 0; count--) {
            u32 sum = (u32)acc + frac;
            u16 k;

            acc = (u16)sum;
            if (sum > 0xffff) {
                line_plot(l, p, c);
                c = (u16)(c + dc);
                p += pitch;
            }
            for (k = step; k != 0; k--) {
                line_plot(l, p, c);
                c = (u16)(c + dc);
                p += pitch;
            }
            p++;
        }
    } else {
        dc = (u16)sw_div(l->dc, cols);
        step = (u16)((u16)cols / (u16)rows);
        frac = (u16)((((u32)((u16)cols % (u16)rows)) << 16) / (u16)rows);
        for (count = (u16)rows; count != 0; count--) {
            u32 sum = (u32)acc + frac;
            u16 k;

            acc = (u16)sum;
            if (sum > 0xffff) {
                line_plot(l, p, c);
                c = (u16)(c + dc);
                p++;
            }
            for (k = step; k != 0; k--) {
                line_plot(l, p, c);
                c = (u16)(c + dc);
                p++;
            }
            p += pitch;
        }
    }
}

/* The cuts of 0x451965..0x451c23 (and 0x454b86..0x454f7f): an end out
 * of the clip moves to its edge, the other coordinate and the colour by
 * the share of the run left. */
static void line_clip(struct line *l, s32 x0, s32 y0, s32 x1, s32 y1)
{
    s32 t;

    if (x0 < r_49c9fc) {
        if (x1 < r_49c9fc) {
            return;
        }
        r_4afe00 = share(x1 - r_49c9fc + 1, x1 - x0 + 1);
        t = y1 - y0;
        y0 += t - sw_mulhi(sw_add(t, t), r_4afe00);
        x0 = r_49c9fc;
        t = sw_add(l->c, l->dc);
        l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
        l->c = sw_sub(t, l->dc);
    }
    if (x1 > r_49ca04) {
        if (x0 > r_49ca04) {
            return;
        }
        r_4afe00 = share(r_49ca04 - x0 + 1, x1 - x0 + 1);
        y1 = y0 + sw_mulhi(sw_add(y1 - y0, y1 - y0), r_4afe00);
        x1 = r_49ca04;
        l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
    }
    if (y0 <= y1) {
        if (y1 > r_49ca08) {
            if (y0 > r_49ca08) {
                return;
            }
            r_4afe00 = share(r_49ca08 - y0 + 1, y1 - y0 + 1);
            x1 = x0 + sw_mulhi(sw_add(x1 - x0, x1 - x0), r_4afe00);
            y1 = r_49ca08;
            l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
        }
        if (y0 < r_49ca00) {
            if (y1 < r_49ca00) {
                return;
            }
            r_4afe00 = share(y1 - r_49ca00 + 1, y1 - y0 + 1);
            x0 = x1 - sw_mulhi(sw_add(x1 - x0, x1 - x0), r_4afe00);
            y0 = r_49ca00;
            t = sw_add(l->c, l->dc);
            l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
            l->c = sw_sub(t, l->dc);
        }
    } else {
        if (y0 > r_49ca08) {
            if (y1 > r_49ca08) {
                return;
            }
            r_4afe00 = share(r_49ca08 - y1 + 1, y0 - y1 + 1);
            x0 = x1 - sw_mulhi(sw_add(x1 - x0, x1 - x0), r_4afe00);
            y0 = r_49ca08;
            t = sw_add(l->c, l->dc);
            l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
            l->c = sw_sub(t, l->dc);
        }
        if (y1 < r_49ca00) {
            if (y0 < r_49ca00) {
                return;
            }
            r_4afe00 = share(y0 - r_49ca00 + 1, y0 - y1 + 1);
            x1 = x0 + sw_mulhi(sw_add(x1 - x0, x1 - x0), r_4afe00);
            y1 = r_49ca00;
            l->dc = sw_mulhi(sw_add(l->dc, l->dc), r_4afe00);
        }
    }
    r_4afdf0 = x0;
    r_4afdf4 = y0;
    r_4afdf8 = x1;
    r_4afdfc = y1;
    line_draw(l, x0, y0, x1, y1);
}

static void line_ends(const struct sw_cmd_line *c, struct line *l)
{
    s32 x0 = c->x0 >> 8, y0 = c->y0 >> 8, x1 = c->x1 >> 8, y1 = c->y1 >> 8, t;

    if (x0 > x1) {
        t = x0, x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
    }
    line_clip(l, x0, y0, x1, y1);
}

/* 0x451747: type 11, a line of table[colour << 8 | f20 >> 8]. */
void sw_raster_line(const struct sw_cmd_line *c)
{
    struct line l;

    l.colour = g_sw_point_table[(u32)(u8)c->colour << 8 | (u8)(c->f20 >> 8)];
    l.dc = 0;
    l.c = 0;
    l.row = NULL;
    l.shade = 0;
    line_ends(c, &l);
}

/* 0x4549a3
 * Type 12: a line whose colour runs from f20 to f24 (8.8), each pixel
 * table[colour << 8 | c >> 8]. The original adds c0's upper half to the
 * table's address; the faces make c0 0. */
void sw_raster_line_12(const struct sw_cmd_line *c)
{
    struct line l;
    s32 c0 = c->f20, c1 = c->f24, t;

    if ((c->x0 >> 8) > (c->x1 >> 8)) {
        t = c0, c0 = c1, c1 = t;
    }
    l.dc = sw_sub(c1, c0);
    l.c = c0;
    l.row = g_sw_point_table + ((u32)c0 & 0xffff0000u) + ((u32)(u8)c->colour << 8);
    l.colour = 0;
    l.shade = 1;
    line_ends(c, &l);
}

/* 0x451730, 0x45498c: the lines' diagonal steps. */
void sw_hook_451730(int w, int h)
{
    r_4b3cd6 = w + 1;
    r_4b3cda = 1 - w;
}

void sw_hook_45498c(int w, int h)
{
    r_4ba41c = w + 1;
    r_4ba420 = 1 - w;
}

/* 0x450684, 0x452ebc: where the reciprocals are, for 15 and 16. */
void sw_hook_450684(int w, int h)
{
    r_4afdec = (s32)(0x49d3a8 >> 2);
}

void sw_hook_452ebc(int w, int h)
{
    r_4b630c = (s32)(0x49d3a8 >> 2);
}

/* 0x450693's state: the left and right edges of the part being drawn
 * and of the part below the middle vertex, 16.16. */
static u32 r_4afde0, r_4afde6;          /* top part: left, right */
static u32 r_4afdbc, r_4afdc2;          /* bottom part: left, right */
static s32 r_4afdd0, r_4afdd4;          /* their steps a row */
static s32 r_4afdb4, r_4afdb8;
static s32 r_4afdcc, r_4afddc;          /* the top part's first row, rows - 1 */
static s32 r_4afdac, r_4afdb0;          /* the bottom part's */

/* One row of type 15: xl..xr, both ends in. */
static void flat_span(u8 *row, s32 xl, s32 xr, u8 colour)
{
    s16 w;

    if (!span_clip(&xl, &xr)) {
        return;
    }
    w = (s16)(xr - xl);
    if (w < 0) {
        return;
    }
    memset(row + xl, colour, (size_t)w + 1);
}

/* 0x450693
 * Type 15: a triangle of one colour, table[colour << 8 | f2c >> 8]. The
 * vertices are sorted by y and taken to whole pixels; the part down to
 * the middle vertex draws its rows with both edges' first values, the
 * part below steps them first. Rows and columns include both ends. */
void sw_raster_flat(const struct sw_cmd_flat *c)
{
    s32 vy[3] = { c->v[0].y, c->v[1].y, c->v[2].y };
    s32 x[3], y[3], xm, t;
    int i0, i1, i2;
    u8 colour, *row;

    sort3(vy, &i0, &i1, &i2);
    x[0] = c->v[i0].x >> 8;
    y[0] = c->v[i0].y >> 8;
    x[1] = c->v[i1].x >> 8;
    y[1] = c->v[i1].y >> 8;
    x[2] = c->v[i2].x >> 8;
    y[2] = c->v[i2].y >> 8;
    if (y[2] - y[0] > 0x3a98) {
        return;
    }
    for (t = 0; t < 3; t++) {
        x[t] = x[t] < -32000 ? -32000 : x[t] > 32000 ? 32000 : x[t];
    }
    if (y[0] == y[2]) {
        /* 0x450cad: one row, from the leftmost x to the rightmost. */
        s32 l = 0, m = 1, r = 2;

        if (x[l] > x[m]) {
            t = l, l = m, m = t;
        }
        if (x[r] > x[m]) {
            r_4afde6 = (u32)x[r] << 16;
        } else {
            if (!(x[r] > x[l])) {
                l = r;
            }
            r_4afde6 = (u32)x[m] << 16;
        }
        r_4afde0 = (u32)x[l] << 16;
        r_4afdcc = y[l];
        r_4afddc = 0;
    } else if (y[0] == y[1]) {
        /* 0x450ce6: a flat top. */
        s32 l = 0, r = 1;
        u32 rcp;

        if (x[l] > x[r]) {
            l = 1, r = 0;
        }
        r_4afdcc = y[l];
        r_4afddc = y[2] - y[l];
        rcp = g_sw_recip[r_4afddc];
        edge_right(x[r], x[2] - x[r], rcp, x[2] - x[r] > 0, &r_4afde6, &r_4afdd4);
        edge_left(x[l], x[2] - x[l], rcp, x[2] - x[l] >= 0, &r_4afde0, &r_4afdd0);
    } else if (y[1] == y[2]) {
        /* 0x450d8f: a flat bottom. */
        s32 l = 1, r = 2;
        u32 rcp;

        if (x[l] > x[r]) {
            l = 2, r = 1;
        }
        r_4afdcc = y[0];
        r_4afddc = y[r] - y[0];
        rcp = g_sw_recip[r_4afddc];
        edge_right(x[0], x[r] - x[0], rcp, x[r] - x[0] > 0, &r_4afde6, &r_4afdd4);
        edge_left(x[0], x[l] - x[0], rcp, x[l] - x[0] >= 0, &r_4afde0, &r_4afdd0);
    } else {
        u32 rcp02 = g_sw_recip[y[2] - y[0]], rcp01, rcp12;

        r_4afdcc = y[0];
        r_4afdac = y[1];
        r_4afddc = y[1] - y[0];
        r_4afdb0 = y[2] - y[1];
        rcp01 = g_sw_recip[r_4afddc];
        rcp12 = g_sw_recip[r_4afdb0];
        /* The long edge's x on the middle vertex's row. */
        xm = (sw_add(sw_mul(sw_mul(x[2] - x[0], (s32)rcp02), r_4afddc), 0x8000) >> 16) + x[0];
        if (xm < x[1]) {
            /* The long edge on the left. */
            edge_left(x[0], x[2] - x[0], rcp02, x[2] - x[0] > 0, &r_4afde0, &r_4afdd0);
            edge_left(xm, x[2] - x[0], rcp02, x[2] - x[0] > 0, &r_4afdbc, &r_4afdb4);
            edge_right(x[0], x[1] - x[0], rcp01, x[1] - x[0] >= 0, &r_4afde6, &r_4afdd4);
            edge_right(x[1], x[2] - x[1], rcp12, x[2] - x[1] > 0, &r_4afdc2, &r_4afdb8);
        } else {
            edge_right(x[0], x[2] - x[0], rcp02, x[2] - x[0] >= 0, &r_4afde6, &r_4afdd4);
            edge_right(xm, x[2] - x[0], rcp02, x[2] - x[0] >= 0, &r_4afdc2, &r_4afdb8);
            edge_left(x[0], x[1] - x[0], rcp01, x[1] - x[0] > 0, &r_4afde0, &r_4afdd0);
            edge_left(x[1], x[2] - x[1], rcp12, x[2] - x[1] >= 0, &r_4afdbc, &r_4afdb4);
        }
    }
    colour = g_sw_point_table[(u32)(u8)c->colour << 8 | (u8)(c->f2c >> 8)];

    /* The top part (0x450a15, 0x450e45): rows y .. y + count. */
    {
        s32 yy = r_4afdcc, n, end;
        int draw = 1;

        if (yy < r_49ca00) {
            n = r_49ca00 - yy;
            r_4afddc -= n;
            if (r_4afddc < 0) {
                draw = 0;
            } else {
                r_4afde6 += (u32)sw_mul(r_4afdd4, n);
                r_4afde0 += (u32)sw_mul(r_4afdd0, n);
                r_4afdcc = yy = r_49ca00;
            }
        }
        if (draw) {
            end = yy + r_4afddc;
            if (end > r_49ca08) {
                r_4afddc -= end - r_49ca08;
                draw = r_4afddc >= 0;
            }
        }
        if (draw) {
            row = g_sw_dst + g_sw_rows[r_4afdcc];
            do {
                flat_span(row, edge_int(r_4afde0) & 0xffff, edge_int(r_4afde6) & 0xffff,
                          colour);
                r_4afde6 += (u32)r_4afdd4;
                r_4afde0 += (u32)r_4afdd0;
                row += g_sw_pitch;
            } while (--r_4afddc >= 0);
        }
    }
    if (y[0] == y[2] || y[0] == y[1] || y[1] == y[2]) {
        return;
    }

    /* The bottom part (0x450ac5): rows y1 + 1 .. y2, each stepping its
     * edges first. */
    r_4afdb0--;
    r_4afdac++;
    if (r_4afdac < r_49ca00) {
        s32 n = r_49ca00 - r_4afdac;

        r_4afdb0 -= n;
        if (r_4afdb0 < 0) {
            return;
        }
        r_4afdc2 += (u32)sw_mul(r_4afdb8, n);
        r_4afdbc += (u32)sw_mul(r_4afdb4, n);
        r_4afdac = r_49ca00;
    }
    if (r_4afdac + r_4afdb0 > r_49ca08) {
        r_4afdb0 -= r_4afdac + r_4afdb0 - r_49ca08;
        if (r_4afdb0 < 0) {
            return;
        }
    }
    row = g_sw_dst + g_sw_rows[r_4afdac];
    do {
        r_4afdc2 += (u32)r_4afdb8;
        r_4afdbc += (u32)r_4afdb4;
        flat_span(row, edge_int(r_4afdbc) & 0xffff, edge_int(r_4afdc2) & 0xffff, colour);
        row += g_sw_pitch;
    } while (--r_4afdb0 >= 0);
}

/* 0x452ecb's state: a copy of the command, its x and y in pixels, and
 * the edges and intensities 0x4535ab draws a part with. */
static struct sw_cmd_flat16 s_copy;     /* 0x4b3ce8 */
static u32 r_4b3d74, r_4b3d7a;          /* left, right x, 16.16 */
static s32 r_4b3d5c, r_4b3d60;
static s32 r_4b3d80, r_4b3d84;          /* left, right intensity, 8.16 */
static s32 r_4b3d64, r_4b3d68;
static s32 r_4b3d88, r_4b3d70;          /* first row, rows - 1 */
/* The part below the middle vertex, kept for the second call. */
static s32 r_4b3d20, r_4b3d24;
static u32 r_4b3d30, r_4b3d36;
static s32 r_4b3d28, r_4b3d2c, r_4b3d3c, r_4b3d40, r_4b3d44, r_4b3d48;

/* The spans 0x4535ab gathers (0x4b3d8c): offset, width, intensity at
 * the right end and its step leftward. */
static struct {
    s32 offset, width, i, di;
    s32 x;                              /* the port's: the left end */
} s_spans[601];

/* 0x4535ab
 * Draws the rows r_4b3d88 .. + r_4b3d70 of a gouraud part: each from its
 * right end leftward, table[colour << 8 | i >> 16], the intensity
 * stepped in a 32 bit register rotated by 8 whose carry comes in a pixel
 * late, as the original's adc does. */
static void gouraud_part(void)
{
    s32 y = r_4b3d88, n, end, row_ofs, count = 0;
    const u8 *row;
    int k;

    if (y < r_49ca00) {
        n = r_49ca00 - y;
        r_4b3d70 -= n;
        if (r_4b3d70 < 0) {
            return;
        }
        r_4b3d7a += (u32)sw_mul(r_4b3d60, n);
        r_4b3d74 += (u32)sw_mul(r_4b3d5c, n);
        r_4b3d80 = sw_add(r_4b3d80, sw_mul(r_4b3d64, n));
        r_4b3d84 = sw_add(r_4b3d84, sw_mul(r_4b3d68, n));
        r_4b3d88 = y = r_49ca00;
    }
    end = y + r_4b3d70;
    if (end > r_49ca08) {
        r_4b3d70 -= end - r_49ca08;
        if (r_4b3d70 < 0) {
            return;
        }
    }
    row_ofs = sw_mul(r_4b3d88, g_sw_pitch);
    do {
        /* The integer halves as words, zero extended. */
        s32 xl = (s32)(u16)(r_4b3d74 >> 16), xr = (s32)(u16)(r_4b3d7a >> 16), w;

        s_spans[count].i = r_4b3d84;
        w = xr - xl;
        if (w != 0) {
            s_spans[count].di = sw_div(r_4b3d80 - r_4b3d84, w);
        }
        if ((s16)xl < (s16)r_49c9fc) {
            if ((s16)xr < (s16)r_49c9fc) {
                goto next;
            }
            xl = r_49c9fc;
        }
        if ((s16)xr > (s16)r_49ca04) {
            if ((s16)xl > (s16)r_49ca04) {
                goto next;
            }
            s_spans[count].i = sw_add(s_spans[count].i,
                                      sw_mul(s_spans[count].di, xr - r_49ca04));
            xr = r_49ca04;
        }
        s_spans[count].width = xr - xl;
        s_spans[count].offset = xl + row_ofs;
        s_spans[count].x = xl;
        if (count < 600) {
            count++;
        }
next:
        row_ofs += g_sw_pitch;
        r_4b3d80 = sw_add(r_4b3d80, r_4b3d64);
        r_4b3d84 = sw_add(r_4b3d84, r_4b3d68);
        r_4b3d7a += (u32)r_4b3d60;
        r_4b3d74 += (u32)r_4b3d5c;
    } while (--r_4b3d70 >= 0);

    row = g_sw_point_table + ((u32)(u8)s_copy.colour << 8);
    for (k = 0; k < count; k++) {
        u32 b = (u32)s_spans[k].i, s = (u32)s_spans[k].di, ec = (u32)s_spans[k].width;
        u8 *p = g_sw_dst + s_spans[k].offset;
        s32 xl = s_spans[k].x;
        unsigned cf = 0;

        b = b >> 8 | b << 24;
        s = s >> 8 | s << 24;
        do {
            u8 v = row[(b >> 8) & 0xff];
            u64 sum = (u64)b + s + cf;
            s32 x = xl + (s32)ec;

            b = (u32)sum;
            cf = (unsigned)(sum >> 32);
            /* The original writes where ecx points; a width gone
             * negative by a lot would write far off. Only the clip's
             * columns are written here. */
            if (x >= r_49c9fc && x <= r_49ca04) {
                p[(s32)ec] = v;
            }
            ec = (ec & 0xffff0000u) | (u16)(ec - 1);
        } while ((s16)(u16)ec >= 0);
    }
}

/* The intensity step of a run of `dy` rows: (di * rcp) >> 8. */
static s32 ramp(s32 di, u32 rcp)
{
    return sw_mul(di, (s32)rcp) >> 8;
}

/* 0x452ecb
 * Type 16: a gouraud triangle of one colour, table[colour << 8 | i]
 * with i each vertex's 8.8 fourth word. The command is copied with x
 * and y in pixels, clamped to -16000..16000. The part down to the
 * middle vertex and the part from it down are drawn by two calls of
 * 0x4535ab, so the middle row is drawn twice. */
void sw_raster_flat16(const struct sw_cmd_flat16 *c)
{
    s32 vy[3], x[3], y[3], i[3], xm, dy02, t;
    int k, i0, i1, i2;

    s_copy = *c;
    for (k = 0; k < 3; k++) {
        s32 a = c->v[k].x >> 8, b = c->v[k].y >> 8;

        s_copy.v[k].x = a < -16000 ? -16000 : a > 16000 ? 16000 : a;
        s_copy.v[k].y = b < -16000 ? -16000 : b > 16000 ? 16000 : b;
        vy[k] = s_copy.v[k].y;
    }
    sort3(vy, &i0, &i1, &i2);
    x[0] = s_copy.v[i0].x, y[0] = s_copy.v[i0].y, i[0] = s_copy.v[i0].i;
    x[1] = s_copy.v[i1].x, y[1] = s_copy.v[i1].y, i[1] = s_copy.v[i1].i;
    x[2] = s_copy.v[i2].x, y[2] = s_copy.v[i2].y, i[2] = s_copy.v[i2].i;
    dy02 = y[2] - y[0];
    if (dy02 > 0x3a98) {
        return;
    }
    if (y[0] == y[2]) {
        /* 0x453378: one row. */
        s32 l = 0, m = 1, r = 2, xr, ir;

        if (x[l] > x[m]) {
            t = l, l = m, m = t;
        }
        xr = x[r];
        ir = i[r];
        if (!(x[r] > x[m])) {
            if (!(x[r] > x[l])) {
                l = r;
            }
            xr = x[m];
            ir = i[m];
        }
        r_4b3d7a = (r_4b3d7a & 0xffff) | (u32)xr << 16;
        r_4b3d74 = (r_4b3d74 & 0xffff) | (u32)x[l] << 16;
        r_4b3d88 = y[l];
        r_4b3d70 = 0;
        r_4b3d84 = sw_mul(ir, 256);
        r_4b3d80 = sw_mul(i[l], 256);
        gouraud_part();
        return;
    }
    if (y[0] == y[1]) {
        /* 0x4533d1: a flat top. */
        s32 l = 0, r = 1;
        u32 rcp;

        if (x[l] > x[r]) {
            l = 1, r = 0;
        }
        r_4b3d88 = y[l];
        r_4b3d70 = y[2] - y[l];
        rcp = g_sw_recip[r_4b3d70];
        r_4b3d80 = sw_mul(i[l], 256);
        r_4b3d84 = sw_mul(i[r], 256);
        edge_right(x[r], x[2] - x[r], rcp, x[2] - x[r] > 0, &r_4b3d7a, &r_4b3d60);
        edge_left(x[l], x[2] - x[l], rcp, x[2] - x[l] >= 0, &r_4b3d74, &r_4b3d5c);
        r_4b3d64 = ramp(i[2] - i[l], rcp);
        r_4b3d68 = ramp(i[2] - i[r], rcp);
        gouraud_part();
        return;
    }
    if (y[1] == y[2]) {
        /* 0x4534c2: a flat bottom. */
        s32 l = 1, r = 2;
        u32 rcp;

        if (x[l] > x[r]) {
            l = 2, r = 1;
        }
        r_4b3d80 = r_4b3d84 = sw_mul(i[0], 256);
        r_4b3d88 = y[0];
        r_4b3d70 = y[r] - y[0];
        rcp = g_sw_recip[r_4b3d70];
        edge_right(x[0], x[r] - x[0], rcp, x[r] - x[0] > 0, &r_4b3d7a, &r_4b3d60);
        edge_left(x[0], x[l] - x[0], rcp, x[l] - x[0] >= 0, &r_4b3d74, &r_4b3d5c);
        r_4b3d64 = ramp(i[l] - i[0], rcp);
        r_4b3d68 = ramp(i[r] - i[0], rcp);
        gouraud_part();
        return;
    }
    {
        u32 rcp02 = g_sw_recip[dy02], rcp02m = g_sw_recip[dy02 - 1], rcp01, rcp01m, rcp12, rcp12m;
        s32 dy01 = y[1] - y[0];

        r_4b3d88 = y[0];
        r_4b3d20 = y[1];
        r_4b3d70 = dy01;
        r_4b3d24 = y[2] - y[1];
        rcp01 = g_sw_recip[dy01];
        rcp01m = g_sw_recip[dy01 - 1];
        rcp12 = g_sw_recip[r_4b3d24];
        rcp12m = g_sw_recip[r_4b3d24 - 1];
        r_4b3d80 = r_4b3d84 = sw_mul(i[0], 256);
        xm = (sw_add(sw_mul(sw_mul(x[2] - x[0], (s32)rcp02), dy01), 0x8000) >> 16) + x[0];
        if (xm < x[1]) {
            u32 f;

            r_4b3d44 = sw_add(sw_mul(sw_mul(i[2] - i[0], (s32)rcp02m), dy01) >> 8,
                              sw_mul(i[0], 256));
            edge_left(x[0], x[2] - x[0], rcp02, x[2] - x[0] > 0, &r_4b3d74, &r_4b3d5c);
            edge_left(xm, x[2] - x[0], rcp02, x[2] - x[0] > 0, &f, &r_4b3d28);
            r_4b3d30 = f;
            r_4b3d64 = r_4b3d3c = ramp(i[2] - i[0], rcp02m);
            edge_right(x[0], x[1] - x[0], rcp01, x[1] - x[0] >= 0, &r_4b3d7a, &r_4b3d60);
            r_4b3d68 = ramp(i[1] - i[0], rcp01m);
            r_4b3d48 = sw_mul(i[1], 256);
            edge_right(x[1], x[2] - x[1], rcp12, x[2] - x[1] > 0, &r_4b3d36, &r_4b3d2c);
            r_4b3d40 = ramp(i[2] - i[1], rcp12m);
        } else {
            /* The original takes this part's right intensity with the
             * long edge's reciprocal, not one less. */
            r_4b3d48 = sw_add(sw_mul(sw_mul(i[2] - i[0], (s32)rcp02), dy01) >> 8,
                              sw_mul(i[0], 256));
            edge_right(x[0], x[2] - x[0], rcp02, x[2] - x[0] >= 0, &r_4b3d7a, &r_4b3d60);
            edge_right(xm, x[2] - x[0], rcp02, x[2] - x[0] >= 0, &r_4b3d36, &r_4b3d2c);
            r_4b3d68 = r_4b3d40 = ramp(i[2] - i[0], rcp02m);
            edge_left(x[0], x[1] - x[0], rcp01, x[1] - x[0] > 0, &r_4b3d74, &r_4b3d5c);
            r_4b3d64 = ramp(i[1] - i[0], rcp01m);
            r_4b3d44 = sw_mul(i[1], 256);
            edge_left(x[1], x[2] - x[1], rcp12, x[2] - x[1] >= 0, &r_4b3d30, &r_4b3d28);
            r_4b3d3c = ramp(i[2] - i[1], rcp12m);
        }
        gouraud_part();
        r_4b3d88 = r_4b3d20;
        r_4b3d70 = r_4b3d24;
        r_4b3d74 = r_4b3d30;
        r_4b3d7a = r_4b3d36;
        r_4b3d5c = r_4b3d28;
        r_4b3d60 = r_4b3d2c;
        r_4b3d64 = r_4b3d3c;
        r_4b3d68 = r_4b3d40;
        r_4b3d80 = r_4b3d44;
        r_4b3d84 = r_4b3d48;
        gouraud_part();
    }
}

/* 0x454604's state. */
static s32 r_4b6500, r_4b6504;          /* left, right x, 16.16 */
static s32 r_4b6508, r_4b650c;          /* their steps a row */
static s32 r_4b64f8, r_4b64fc;          /* the first row, the rows */
static const u8 *r_4b6510;              /* the table's row of the colour */

/* The 16.16 step of the edge a..b (8.8 points) and its x on the first
 * whole row: a flat run (under a quarter pixel high) takes 0x41 for its
 * height, or 3 if it is wider than 256 pixels. */
static void edge_19(const s32 *a, const s32 *b, s32 *start, s32 *step)
{
    u32 dy = (u32)(b[1] - a[1]);
    s32 dx = b[0] - a[0], ax = dx > 0 ? dx : -dx;
    int64_t q;

    if (dy <= 0x40) {
        if ((u32)ax <= 0x10000) {
            dy = 0x41;
        } else if (dy <= 3) {
            dy = 3;
        }
    }
    /* idiv faults on a quotient past 32 bits; the port keeps its low
     * half. */
    q = ((int64_t)dx * 65536) / (s32)dy;
    *step = (s32)q;
    *start = sw_add(sw_mul(0x100 - (a[1] & 0xff), *step >> 8), (s32)((u32)a[0] << 8));
}

/* 0x4548c0, 0x454926: the two edges' starts and steps. */
static void edge_4548c0(const s32 *a, const s32 *b)
{
    edge_19(a, b, &r_4b6500, &r_4b6508);
}

static void edge_454926(const s32 *a, const s32 *b)
{
    edge_19(a, b, &r_4b6504, &r_4b650c);
}

/* 0x4547c8
 * The rows r_4b64f8 .. + r_4b64fc - 1 between the two edges, each pixel
 * table[colour << 8 | dst]; a row covers left <= x < right. */
static void rows_4547c8(void)
{
    s32 a = r_4b6500, b = r_4b6504, y = r_4b64f8, n;
    u8 *row;

    if (y < r_49ca00) {
        n = r_49ca00 - y;
        r_4b64fc -= n;
        if (r_4b64fc <= 0) {
            return;
        }
        a = sw_add(a, sw_mul(n, r_4b6508));
        b = sw_add(b, sw_mul(n, r_4b650c));
        y = r_49ca00;
    }
    n = r_49ca18 - y;
    if (n < r_4b64fc) {
        if (y > r_49ca18 || n == 0) {
            return;
        }
        r_4b64fc = n;
    }
    row = g_sw_dst + g_sw_rows[y];
    do {
        s32 xl = a >> 16, xr = b >> 16;

        if (xl < r_49c9fc) {
            if (xr < r_49c9fc) {
                goto next;
            }
            xl = r_49c9fc;
        }
        if (xr > r_49ca14) {
            if (xl > r_49ca14) {
                goto next;
            }
            xr = r_49ca14;
        }
        for (; xl < xr; xl++) {
            row[xl] = r_4b6510[row[xl]];
        }
next:
        a = sw_add(a, r_4b6508);
        b = sw_add(b, r_4b650c);
        row += g_sw_pitch;
    } while (--r_4b64fc != 0);
}

/* 0x454604
 * Type 19: a triangle through a table, table[colour << 8 | dst]. The
 * vertices stay in 8.8; a vertex's x outside -32512..32000 pixels (or
 * the last one's y past 32000) leaves the triangle out. Rows run from
 * the first whole row to the one before the last vertex's. */
void sw_raster_flat_table(const struct sw_cmd_flat_table *c)
{
    s32 vy[3] = { c->v[0].y, c->v[1].y, c->v[2].y };
    s32 p[3][2], dy;
    int i0, i1, i2, k;
    int idx[3];

    r_4b6510 = c->table + ((u32)(u8)c->colour << 8);
    sort3(vy, &i0, &i1, &i2);
    idx[0] = i0;
    idx[1] = i1;
    idx[2] = i2;
    for (k = 0; k < 3; k++) {
        s32 x = c->v[idx[k]].x;

        if (x < (s32)0xff830000 || x > 0x7d0000) {
            return;
        }
        p[k][0] = x;
        p[k][1] = c->v[idx[k]].y;
    }
    if (p[2][1] > 0x7d0000) {
        return;
    }
    r_4b64f8 = p[0][1] >> 8;
    dy = (p[1][1] >> 8) - r_4b64f8;
    if (dy != 0) {
        r_4b64fc = dy;
        edge_4548c0(p[0], p[1]);
        edge_454926(p[0], p[2]);
        if (r_4b6508 > r_4b650c) {
            s32 t = r_4b6508;

            r_4b6508 = r_4b650c;
            r_4b650c = t;
            t = r_4b6500;
            r_4b6500 = r_4b6504;
            r_4b6504 = t;
        }
        rows_4547c8();
    }
    r_4b64f8 = p[1][1] >> 8;
    dy = (p[2][1] >> 8) - r_4b64f8;
    if (dy == 0) {
        return;
    }
    r_4b64fc = dy;
    edge_4548c0(p[0], p[2]);
    edge_454926(p[1], p[2]);
    r_4b6500 = sw_add(r_4b6500, sw_mul((p[1][1] >> 8) - (p[0][1] >> 8), r_4b6508));
    if (r_4b6508 < r_4b650c) {
        s32 t = r_4b6508;

        r_4b6508 = r_4b650c;
        r_4b650c = t;
        t = r_4b6500;
        r_4b6500 = r_4b6504;
        r_4b6504 = t;
    }
    rows_4547c8();
}
