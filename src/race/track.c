/*
 * race/track.c - the track's road pieces: their types, and the order
 * they run in (0x416250, 0x41b360, 0x41b400).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/mem.h"
#include "race/race.h"
#include "scene.h"

struct plc_extra *g_plc_extra;          /* 0x552e40 */
struct road_order *g_road_order;        /* 0x553784 */
struct track_link *g_track_links;       /* 0x552f60 */
s32 g_track_link_count;                 /* 0x5287f8 */
s32 g_road_count;                       /* 0x553074 */
u8 *g_colour_key;                       /* 0x563cf8 */

enum { MAX_PIECES = 1500 };

/* 0x41b360: splits each placement's type into its low 12 bits and
 * three fields. */
void race_unpack_types(void)
{
    int n = g_level_plc[0], i;

    g_plc_extra = mem_calloc(g_race_pool, (u32)n * sizeof(*g_plc_extra));
    for (i = 0; i < n; i++) {
        struct plc_entry *e = plc_at(g_level_plc, i);
        u32 t = (u32)e->type;

        g_plc_extra[i].f0 = (s32)((t & 0xf000) >> 12);
        g_plc_extra[i].f1 = (s32)((t & 0xff0000) >> 16);
        g_plc_extra[i].f2 = (s32)(t >> 24);
        e->type = (s32)(t & 0xfff);
    }
}

/* 0x402940 */
static void make_colour_key(u8 *t)
{
    int s, d;

    for (s = 0; s < 256; s++) {
        for (d = 0; d < 256; d++) {
            t[s << 8 | d] = (u8)(s != 0 ? s : d);
        }
    }
}

/* 0x41b400 */
void race_make_tables(void)
{
    u8 *buf = mem_calloc(g_race_pool, 0x20000);

    g_race_bytes += 0x20000;
    if (buf == NULL) {
        mem_out_of_memory();
    }
    g_colour_key = (u8 *)(((uintptr_t)buf + 0xffff) & ~(uintptr_t)0xffff);
    make_colour_key(g_colour_key);
}

static s32 *mesh_of(const struct plc_entry *e)
{
    return g_level_msh + e->mesh;
}

/* Moves a mesh's vertices by a placement (sign 1) or back (-1); mesh
 * Y points down. */
static void move_mesh(const struct plc_entry *e, int sign)
{
    s32 *m = mesh_of(e);
    s32 *v = m + 2;
    int i;

    for (i = 0; i < m[0]; i++, v += 3) {
        v[0] += sign * e->x;
        v[1] -= sign * e->y;
        v[2] += sign * e->z;
    }
}

/* Whether a face counts towards a road piece's centre: pass 0 takes
 * the road materials, pass 1 any material below 40, pass 2 any face. */
static int centre_face(u32 flags, int pass)
{
    s32 mat = (s32)(flags >> 16);

    if (pass == 2) {
        return 1;
    }
    if (mat >= 40) {
        return 0;
    }
    if (pass == 1) {
        return 1;
    }
    mat %= 10;
    return mat < 5 || mat == 6;
}

/* Puts a road piece's origin at the middle of its road faces. */
static void centre_piece(struct plc_entry *e)
{
    s32 *m = mesh_of(e);
    const s32 *verts = m + 2;
    const s32 *faces = verts + 3 * m[0];
    s32 sum[3] = { 0, 0, 0 };
    int count = 0, pass, i, k;

    move_mesh(e, 1);
    for (pass = 0; pass < 3 && count == 0; pass++) {
        sum[0] = sum[1] = sum[2] = 0;
        for (i = 0; i < m[1]; i++) {
            const s32 *f = faces + 11 * i;

            if (!centre_face((u32)f[0], pass)) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                const s32 *v = verts + 3 * f[1 + k];

                sum[0] += v[0];
                sum[1] += v[1];
                sum[2] += v[2];
            }
            count += 3;
        }
    }
    if (count != 0) {
        e->x = sum[0] / count;
        e->y = sum[1] / count;
        e->z = sum[2] / count;
    }
    move_mesh(e, -1);
}

/* The heading from b to a: -atan2(dx, dz), in 0..2 pi. */
static double heading(const struct plc_entry *a, const struct plc_entry *b)
{
    double h = -atan2((double)(a->x - b->x), (double)(a->z - b->z));

    if (h < 0.0) {
        h += 6.2831853071796;
    }
    if (h >= 6.2831853071796) {
        h -= 6.2831853071796;
    }
    return h;
}

/* The nearest unvisited placement to `from` of the types in `types`
 * (ended by -1); height counts double. Returns `fallback` if none. */
static int nearest(int from, const s32 *types, const u8 *visited, int fallback)
{
    const struct plc_entry *a = plc_at(g_level_plc, from);
    double best = 1e8;
    int n = g_level_plc[0], i, found = fallback;

    for (i = 0; i < n; i++) {
        const struct plc_entry *b = plc_at(g_level_plc, i);
        double dx, dy, dz, d;
        int k, ok = 0;

        if (i == from || visited[i]) {
            continue;
        }
        for (k = 0; types[k] >= 0; k++) {
            ok |= b->type == types[k];
        }
        if (!ok) {
            continue;
        }
        dx = a->x - b->x;
        dy = 2.0 * (a->y - b->y);
        dz = a->z - b->z;
        d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best) {
            best = d;
            found = i;
        }
    }
    return found;
}

static void link(int at, int plc, double h, int order)
{
    g_road_order[plc].order = order;
    g_road_order[plc].heading = h;
    g_track_links[at].plc = plc;
    g_track_links[at].heading = h;
    g_track_links[at].alt_plc = -1;
    g_track_links[at].alt_heading = -1.0;
}

static int first_of_type(int type, int fallback)
{
    int i;

    for (i = 0; i < g_level_plc[0]; i++) {
        if (plc_at(g_level_plc, i)->type == type) {
            return i;
        }
    }
    return fallback;
}

/* 0x416250
 * Road pieces are numbered by type: 0 the start, 1 the piece after it,
 * 2, 3, 7 and 9 the road, 3 a fork whose branches are 4 .. 6 and
 * 5 .. 6, 7 pairs (each joins the nearest other 7). From the start the
 * order follows the nearest unvisited piece. The original also moves
 * the type 300 meshes there and back, which changes nothing. */
void race_track_order(void)
{
    static const s32 road[] = { 2, 3, 7, 9, -1 };
    static const s32 branch_a[] = { 4, 6, -1 };
    static const s32 branch_b[] = { 5, 6, -1 };
    static const s32 pair[] = { 7, -1 };
    u8 visited[MAX_PIECES];
    int n = g_level_plc[0], i, start, cur, best = 0, at, steps, limit;

    g_road_order = mem_calloc(g_race_pool, MAX_PIECES * sizeof(*g_road_order));
    g_track_links = mem_calloc(g_race_pool, MAX_PIECES * sizeof(*g_track_links));
    g_road_count = 0;
    for (i = 0; i < n; i++) {
        struct plc_entry *e = plc_at(g_level_plc, i);

        if (e->type > 50) {
            continue;
        }
        centre_piece(e);
        if (e->type < 50 && e->type != 8) {
            g_road_count++;
        }
    }

    memset(visited, 0, sizeof(visited));
    for (i = 0; i < MAX_PIECES; i++) {
        g_road_order[i].order = 10000;
    }
    start = first_of_type(0, n);
    cur = first_of_type(1, n);
    if (start >= n || cur >= n) {
        g_track_link_count = 0;
        return;
    }
    visited[start] = 1;
    link(0, start, heading(plc_at(g_level_plc, start), plc_at(g_level_plc, cur)), 0);
    at = 1;
    limit = g_road_count - 3;
    for (steps = 0; steps < limit; steps++) {
        best = nearest(cur, road, visited, best);
        visited[cur] = 1;
        link(at, cur, heading(plc_at(g_level_plc, cur), plc_at(g_level_plc, best)), at);
        at++;
        cur = best;
        if (plc_at(g_level_plc, cur)->type == 3) {
            int fork = cur, fork_at = at, after_a, k;

            /* The first branch, in the links themselves. */
            do {
                best = nearest(cur, branch_a, visited, best);
                visited[cur] = 1;
                g_road_order[cur].order = at;
                g_road_order[cur].heading =
                    heading(plc_at(g_level_plc, cur), plc_at(g_level_plc, best));
                g_track_links[at].plc = cur;
                g_track_links[at].heading = g_road_order[cur].heading;
                g_track_links[at].alt_plc = -2;
                g_track_links[at].alt_heading = -2.0;
                at++;
                cur = best;
                if (plc_at(g_level_plc, cur)->type != 6) {
                    steps++;
                }
            } while (plc_at(g_level_plc, cur)->type != 6);
            after_a = at;

            /* The second, from the fork again, beside the first. */
            cur = fork;
            k = fork_at;
            do {
                double h;

                best = nearest(cur, branch_b, visited, best);
                h = heading(plc_at(g_level_plc, cur), plc_at(g_level_plc, best));
                g_road_order[cur].order = -k;
                g_road_order[cur].heading = h;
                visited[cur] = 1;
                g_track_links[k].alt_plc = cur;
                g_track_links[k].alt_heading = h;
                k++;
                cur = best;
                if (plc_at(g_level_plc, cur)->type != 6) {
                    steps++;
                }
            } while (plc_at(g_level_plc, cur)->type != 6);
            at = after_a;
        }
        if (plc_at(g_level_plc, cur)->type == 7) {
            best = nearest(cur, pair, visited, best);
            visited[cur] = 1;
            link(at, cur, heading(plc_at(g_level_plc, cur), plc_at(g_level_plc, best)), at);
            at++;
            cur = best;
        }
    }
    best = first_of_type(0, best);
    visited[cur] = 1;
    link(at, cur, heading(plc_at(g_level_plc, cur), plc_at(g_level_plc, best)), at);
    g_track_link_count = at + 1;
}
