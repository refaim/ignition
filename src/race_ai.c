/*
 * race_ai.c - the computer's drivers (0x4134e0..0x4161c0).
 *
 * The racing line comes from the track's .TRI: for each placement two
 * vertices of its mesh, the road's edges across it. Each segment of the
 * track's order gets one node, or two where the road splits into lanes
 * or the order forks. The driver looks a few segments ahead for the
 * farthest node it can see the middle of, steers for it, and sets its
 * speed by the bends and hills before it.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/file.h"
#include "lisa/log.h"
#include "race.h"
#include "race_ai.h"
#include "race_cars.h"
#include "race_control.h"
#include "race_data.h"
#include "race_init.h"

enum {
    AI_NODES = 1500,        /* 0x5dc */
    AI_MARKERS = 50,
    AI_LIST = 3000,         /* 0xbb8 */
    TRI_RECORD = 500,       /* 0x1f4 bytes a placement */
    PIECE_FACES = 50
};

struct ai_state *g_ai;                  /* 0x63a018 */
struct ai_node ***g_ai_lanes;           /* 0x63a024, also 0x63a158 */
s16 g_ai_step_seg, g_ai_step_lane, g_ai_step_piece;     /* 0x63a150.. */

static struct ai_node *s_nodes;         /* 0x63a01c */
static u8 *s_markers;                   /* 0x63a144 */
static struct ai_node **s_list;         /* 0x63a148 */
static s32 r_63a02c;
static s32 s_start_x, s_start_z;        /* 0x63a030, 0x639b30 */
static u32 s_seed = 1;                  /* 0x4bb08c */

/* The track index 0x4c5488 maps each track to for its tables. */
static const s16 s_track_map[7] = { 1, 2, 3, 6, 4, 0, 5 };

/* 0x4694d0 */
void race_srand(u32 seed)
{
    s_seed = seed;
}

/* 0x4694e0 */
int race_msrand(void)
{
    s_seed = s_seed * 214013u + 2531011u;
    return (int)((s_seed >> 16) & 0x7fff);
}

/* The piece being read: its placement, mesh, vertices and faces
 * (0x639b2c, 0x639b24, 0x639b20, 0x63a164). */
struct piece {
    const struct plc_entry *plc;
    const s32 *mesh;
    const s32 *verts;
    const s32 *faces;
};

static void piece_set(struct piece *pc, int plc)
{
    pc->plc = plc_at(g_level_plc, plc);
    pc->mesh = g_level_msh + pc->plc->mesh;
    pc->verts = pc->mesh + 2;
    pc->faces = pc->verts + 3 * pc->mesh[0];
}

/* 0x415ee0
 * The middle of the piece's open edges: of its road faces (types below
 * 40 that are not 5..7 a ten), the vertices one or two of them use, in
 * world units. Only the last piece's is kept. */
static void piece_centre(const struct piece *pc, s32 *x, s32 *z)
{
    s32 faces[PIECE_FACES], edge[PIECE_FACES];
    int nf = 0, ne = 0, v, k;
    s32 sx = 0, sz = 0;

    for (k = 0; k < pc->mesh[1]; k++) {
        s32 type = (s32)((u32)pc->faces[11 * k] >> 16);

        if (type < 40 && type % 10 != 5 && type % 10 != 6 && type % 10 != 7) {
            faces[nf++] = k;
            if (nf == PIECE_FACES) {
                break;
            }
        }
    }
    for (v = 0; v < pc->mesh[0] && ne < PIECE_FACES; v++) {
        int uses = 0;

        for (k = 0; k < nf; k++) {
            const s32 *f = pc->faces + 11 * faces[k];

            uses += (f[1] == v) + (f[2] == v) + (f[3] == v);
        }
        if (uses == 1 || uses == 2) {
            edge[ne++] = v;
        }
    }
    for (k = 0; k < ne; k++) {
        sx += pc->plc->x + pc->verts[3 * edge[k]] + 0x6400;
        sz += pc->verts[3 * edge[k] + 2] + pc->plc->z + 0x6400;
    }
    if (ne > 0) {
        sx /= ne;
        sz /= ne;
    }
    *x = sx;
    *z = sz;
}

static s16 rd16(const u8 *p)
{
    return (s16)(p[0] | p[1] << 8);
}

/* A node from the .TRI record: the two vertices across the piece. */
static void node_from(struct ai_node *n, const struct piece *pc, const u8 *tri)
{
    const s32 *a = pc->verts + 3 * rd16(tri + 1), *b = pc->verts + 3 * rd16(tri + 0x17);

    n->marker = 0;
    n->surface = tri[0];
    n->x1 = a[0] + pc->plc->x + 0x6400;
    n->y1 = a[1] + pc->plc->y;
    n->z1 = a[2] + pc->plc->z + 0x6400;
    n->x2 = b[0] + pc->plc->x + 0x6400;
    n->y2 = b[1] + pc->plc->y;
    n->z2 = b[2] + pc->plc->z + 0x6400;
    n->type = tri[0x6d];
}

/* Halves the way from a to b, rounding toward a as idiv does. */
static s32 halfway(s32 a, s32 b)
{
    return a + (b - a) / 2;
}

/* The headings from node m to node n (the next segment's), and m's
 * points 18 in from its ends. */
static void node_angles(struct ai_node *m, const struct ai_node *n)
{
    m->h1 = (float)atan2((double)(n->z1 - m->z1), (double)(n->x1 - m->x1));
    m->hm = (float)atan2((double)((n->z2 - n->z1) / 2 + (m->z1 - m->z2) / 2 - m->z1 + n->z1),
                         (double)((m->x1 - m->x2) / 2 + (n->x2 - n->x1) / 2 - m->x1 + n->x1));
    m->h2 = (float)atan2((double)(n->z2 - m->z2), (double)(n->x2 - m->x2));
    m->v1 = (float)atan2((double)(n->y1 - m->y1), (double)(n->x1 - m->x1));
    m->vm = (float)atan2((double)((m->y1 - m->y2) / 2 + (n->y2 - n->y1) / 2 - m->y1 + n->y1),
                         (double)((m->x1 - m->x2) / 2 + (n->x2 - n->x1) / 2 - m->x1 + n->x1));
    m->v2 = (float)atan2((double)(n->y2 - m->y2), (double)(n->x2 - m->x2));
    m->across = (float)atan2((double)(m->z2 - m->z1), (double)(m->x2 - m->x1));
    m->back = (float)atan2((double)(m->z1 - m->z2), (double)(m->x1 - m->x2));
    m->ax = (s32)(m->x1 + cos(m->across) * 18.0);
    m->az = (s32)(m->z1 + sin(m->across) * 18.0);
    m->bx = (s32)(m->x2 + cos(m->back) * 18.0);
    m->bz = (s32)(m->z2 + sin(m->back) * 18.0);
}

/* The list slot at k, if it is inside the list. The original writes
 * one before a segment's start when its first branch has no piece. */
static void list_put(int k, struct ai_node *n)
{
    if (k >= 0 && k < AI_LIST) {
        s_list[k] = n;
    } else {
        log_printf("race_ai: list slot %d out of range\n", k);
    }
}

/* 0x414e40 */
void race_load_ai_track(void)
{
    static const double scale[4] = { 1.0, 0.7, 0.4, 0.4 };
    s32 difficulty = race_cfg(0x00c);
    char name[64];
    u8 *tri;
    const u8 **recs;
    struct ai_node *cur;
    u8 *mark;
    struct piece pc;
    int i, s, k = 0, n = g_level_plc[0];
    s32 cx, cz;

    r_63a02c = 0;
    g_ai = calloc((size_t)g_driver_count, sizeof(*g_ai));
    for (i = 1; i < g_driver_count; i++) {
        const double *p = &g_ai_params[g_drivers[i].car * 9];
        struct ai_state *a = &g_ai[i];

        if (difficulty >= 0 && difficulty <= 3) {
            a->skill = difficulty == 0 ? p[0] : p[0] * scale[difficulty];
        }
        a->f08 = p[1];
        a->f10 = p[2];
        a->f18 = p[3];
        a->f20 = p[4];
        a->look = p[5];
        a->f38 = p[6];
        a->f40 = p[7];
        a->f48 = (s32)p[8];
        a->stuck = 0;
        a->lane = 0;
        a->seg = 0;
        a->chance = 0.0;
        a->steer = 0.0;
    }
    race_srand((u32)(s32)(s16)(s32)g_cars[0].x);
    s_nodes = calloc(AI_NODES, sizeof(*s_nodes));
    s_markers = calloc(AI_MARKERS, 1);
    s_list = calloc(AI_LIST, sizeof(*s_list));
    g_ai_lanes = calloc((size_t)g_track_link_count, sizeof(*g_ai_lanes));
    cur = s_nodes;
    mark = s_markers;
    piece_set(&pc, g_track_links[g_track_link_count - 1].plc);
    piece_centre(&pc, &s_start_x, &s_start_z);

    snprintf(name, sizeof(name), "LEVELS\\%s%s.TRI", g_track_dir, g_track_name);
    tri = malloc((size_t)n * TRI_RECORD + 4);
    recs = malloc((size_t)n * sizeof(*recs));
    for (i = 0; i < n; i++) {
        recs[i] = tri + 4 + i * TRI_RECORD;
    }
    if (!file_read_at(name, tri, (u32)n * TRI_RECORD + 4, 0)) {
        log_printf("race_ai: no %s\n", name);
    }

    for (s = 0; s < g_track_link_count; s++) {
        const struct track_link *l = &g_track_links[s];
        int start = k;

        g_ai_lanes[s] = &s_list[k];
        if (l->plc >= 0) {
            const u8 *r = recs[l->plc];

            piece_set(&pc, l->plc);
            piece_centre(&pc, &cx, &cz);
            if (s != 0) {
                race_ai_step((s16)(s - 1), 0, -1);
            }
            node_from(cur, &pc, r);
            k += 2;
            list_put(start, cur);
            list_put(k - 1, NULL);
            cur++;
            if (r[0x6d] == 1) {
                /* The far half first, then the near. */
                struct ai_node *a = cur - 1, *b = cur;
                s32 x1 = a->x1, y1 = a->y1, z1 = a->z1;

                a->x1 = halfway(x1, a->x2);
                a->y1 = halfway(y1, a->y2);
                a->z1 = halfway(z1, a->z2);
                b->marker = 0;
                b->surface = r[0];
                b->x1 = x1;
                b->y1 = y1;
                b->z1 = z1;
                b->x2 = halfway(x1, a->x2);
                k++;
                b->y2 = halfway(y1, a->y2);
                b->z2 = halfway(z1, a->z2);
                b->type = r[0x6d];
                list_put(k - 2, b);
                list_put(k - 1, NULL);
                cur++;
                continue;
            }
            if (r[0x6d] == 2) {
                /* The near half first, then the far. */
                struct ai_node *a = cur - 1, *b = cur;
                s32 x2 = a->x2, y2 = a->y2, z2 = a->z2;

                a->x2 = halfway(a->x1, x2);
                a->y2 = halfway(a->y1, y2);
                a->z2 = halfway(a->z1, z2);
                b->marker = 0;
                b->surface = r[0];
                b->x1 = halfway(a->x1, x2);
                b->y1 = halfway(a->y1, y2);
                k++;
                b->z1 = halfway(a->z1, z2);
                b->x2 = x2;
                b->y2 = y2;
                b->z2 = z2;
                b->type = r[0x6d];
                list_put(k - 2, b);
                list_put(k - 1, NULL);
                cur++;
                continue;
            }
        } else if (l->plc == -2) {
            *mark = 1;
            k += 2;
            list_put(start, (struct ai_node *)mark);
            list_put(k - 1, NULL);
            mark++;
        }
        /* The second branch of a fork. */
        if (l->alt_plc >= 0) {
            piece_set(&pc, l->alt_plc);
            piece_centre(&pc, &cx, &cz);
            if (s != 0) {
                race_ai_step((s16)(s - 1), 1, -1);
            }
            node_from(cur, &pc, recs[l->alt_plc]);
            k++;
            list_put(k - 2, cur);
            list_put(k - 1, NULL);
            cur++;
        } else if (l->alt_plc == -2) {
            *mark = 1;
            k++;
            list_put(k - 2, (struct ai_node *)mark);
            list_put(k - 1, NULL);
            mark++;
        }
    }

    for (s = 0; s < g_track_link_count; s++) {
        struct ai_node *m = g_ai_lanes[s][0];

        if (m != NULL && m->marker == 0) {
            race_ai_step((s16)(s + 1), 0, 1);
            node_angles(m, g_ai_lanes[g_ai_step_seg][g_ai_step_lane]);
        }
        m = g_ai_lanes[s][1];
        if (m != NULL && m->marker == 0) {
            race_ai_step((s16)(s + 1), 1, 1);
            node_angles(m, g_ai_lanes[g_ai_step_seg][g_ai_step_lane]);
        }
    }
    free(tri);
    free(recs);
}

/* 0x415e90 */
int race_free_ai_track(void)
{
    free(g_ai);
    free(s_nodes);
    free(s_markers);
    free(s_list);
    free(g_ai_lanes);
    g_ai = NULL;
    s_nodes = NULL;
    s_markers = NULL;
    s_list = NULL;
    g_ai_lanes = NULL;
    return 1;
}

/* 0x4160b0
 * From segment `seg`, in `lane` (the other if this segment has only
 * one), moves by `step` past fork markers and the single node where
 * two lanes join, to the next segment with a node to aim at. The
 * Canadian track also skips 171..173. */
void race_ai_step(s16 seg, s16 lane, s16 step)
{
    s16 links = (s16)g_track_link_count;

    if (seg >= links) {
        seg = (s16)(seg - links);
    } else if (seg < 0) {
        seg = (s16)(seg + links);
    }
    for (;;) {
        s16 prev = (s16)(seg - 1);
        struct ai_node **l = g_ai_lanes[seg];

        if (prev < 0) {
            prev = 0;
        }
        if (l[lane] == NULL) {
            lane = (s16)(1 - lane);
        }
        if (l[lane]->marker == 0) {
            if (l[1] == NULL && g_ai_lanes[prev][1] != NULL) {
                goto next;
            }
            if (g_track != 0 || seg < 0xab || seg > 0xad) {
                break;
            }
        }
next:
        seg = (s16)(seg + step);
        if (seg >= links) {
            seg = (s16)(seg - links);
        } else if (seg < 0) {
            seg = (s16)(seg + links);
        }
    }
    g_ai_step_seg = seg;
    g_ai_step_lane = lane;
    g_ai_step_piece = (s16)(lane == 0 ? g_track_links[seg].plc : g_track_links[seg].alt_plc);
}

static const double PI = 3.141596, TWO_PI = 6.283192;

/* Into -pi .. pi once, with the original's rough pi. */
static double wrap_once(double a)
{
    if (a < -PI) {
        a += TWO_PI;
    }
    if (a > PI) {
        a -= TWO_PI;
    }
    return a;
}

/* 0x4134e0
 * One step of computer car i's driving. */
void race_ai(int i)
{
    struct car *c = &g_cars[i];
    struct ai_state *a = &g_ai[i];
    const struct car_phys *p = &g_car_phys[i];
    struct ai_node *look[6], **lanes, *cur;
    s32 seg, s, lane, links = g_track_link_count, tseg, steps, brake = 0, boost = 0;
    s32 difficulty = race_cfg(0x00c), track = s_track_map[g_track % 7];
    s32 near_car, k, n;
    s32 cx1, cz1, cx2, cz2, lx1, lz1, lx2, lz2;
    double d1x, d1z, l1x, l1z, l2x, l2z, d2x, d2z, ratio, turn, rough, bends, t, dist, dy;
    double slope, speed, throttle, brake_amt, limit, aim, drift, head, road, steer, mx = 0, mz = 0;
    double best;

    r_63a02c = 0;
    seg = c->f364;
    if (links <= seg) {
        seg = (g_track == 0 && a->seg > 0x50 && a->seg < 0x5e) ? 0x5e : a->seg;
    }
    s = seg;
    lane = 0;
    if (s < 0) {
        s = -s;
        lane = 1;
    }
    lanes = g_ai_lanes[s];
    if (lanes[1] != NULL && lanes[1]->marker == 0) {
        u8 type = lanes[1]->type;

        if (type == 1 || type == 2) {
            if (a->fork_lane != 0 || g_track_links[s].alt_plc != g_track_links[s].plc) {
                lane = a->lane;
            } else if (type == 1) {
                lane = 0.0 <= a->side ? 0 : 1;
            } else {
                lane = 0.0 < a->side ? 1 : 0;
            }
        }
    }
    /* A fork ahead: pick a branch. */
    for (k = 0; k < 4; k++) {
        const s16 *f = &g_ai_forks[(track * 4 + k) * 3];

        if (f[0] == s) {
            s16 chance;

            a->fork_lane = g_ai_forks[(g_track * 4 + k) * 3 + 2];
            a->chance = race_msrand() * 3.0517578125e-05;
            chance = f[1];
            if (chance == 0) {
                a->lane = 0;
            } else if (chance == 100) {
                a->lane = 1;
            } else {
                a->lane = 1;
                if (!(chance * (a->f20 * 0.7 + 0.3) * a->chance * 0.01 > 0.125)) {
                    a->lane = 0;
                }
            }
            break;
        }
    }
    for (k = 0; k < 6; k++) {
        s32 ls = g_ai_steps[k] + s;

        if (ls >= links) {
            ls -= links;
        }
        if (!(links - 3 > ls)) {
            ls = links - 1;
        }
        race_ai_step((s16)ls, (s16)(lanes[1] == NULL ? a->lane : lane), 1);
        look[k] = g_ai_lanes[g_ai_step_seg][g_ai_step_lane];
    }

    /* Where the car is across the road: the sums of its distances to the
     * edges here and at the next node, as a ratio. */
    cur = lanes[lane];
    cz1 = cur->z1;
    cx1 = cur->x1;
    cx2 = cur->x2;
    cz2 = cur->z2;
    lx1 = look[0]->x1;
    lz1 = look[0]->z1;
    lx2 = look[0]->x2;
    lz2 = look[0]->z2;
    if (cur->type == 1) {
        cx1 = lanes[1]->x1;
        cz1 = lanes[1]->z1;
        cx2 = lanes[0]->x2;
        cz2 = lanes[0]->z2;
    } else if (cur->type == 2) {
        cx1 = lanes[0]->x1;
        cz1 = lanes[0]->z1;
        cx2 = lanes[1]->x2;
        cz2 = lanes[1]->z2;
    }
    {
        s32 ns = s + 1 >= links ? s + 1 - links : s + 1;
        struct ai_node **nl = g_ai_lanes[ns];

        if (look[0]->type == 1) {
            lx1 = nl[1]->x1;
            lz1 = nl[1]->z1;
            lx2 = nl[0]->x2;
            lz2 = nl[0]->z2;
        } else if (look[0]->type == 2) {
            lx1 = nl[0]->x1;
            lz1 = nl[0]->z1;
            lx2 = nl[1]->x2;
            lz2 = nl[1]->z2;
        }
    }
    l1z = lz1 - c->z;
    l1x = lx1 - c->x;
    d1z = cz1 - c->z;
    d1x = cx1 - c->x;
    l2x = lx2 - c->x;
    l2z = lz2 - c->z;
    d2x = cx2 - c->x;
    d2z = cz2 - c->z;
    ratio = (sqrt(d1z * d1z + d1x * d1x) + sqrt(l1x * l1x + l1z * l1z))
            / (sqrt(d2z * d2z + d2x * d2x) + sqrt(l2z * l2z + l2x * l2x));
    if (ratio < 1.0) {
        ratio = -(1.0 / ratio);
    }
    a->side = ratio;
    turn = wrap_once((double)look[0]->hm - (double)cur->hm);

    /* The farthest node ahead whose middle shows between the nearer
     * nodes' edges; `blocked` says on which side one was in the way. */
    {
        int e = 5, blocked = 0;

        for (;;) {
            struct ai_node *m = look[e];
            double dir;
            int j, seen = 1;

            mx = (double)halfway(m->x1, m->x2);
            mz = (double)halfway(m->z1, m->z2);
            dir = atan2(mz - c->z, mx - c->x);
            for (j = 0; j <= e; j++) {
                double al = wrap_once(atan2(mz - look[j]->az, mx - look[j]->ax) - dir);
                double be = wrap_once(atan2(mz - look[j]->bz, mx - look[j]->bx) - dir);

                if (!(al * be < 0.0)) {
                    seen = 0;
                    blocked = 0.0 < al || al == 0.0 ? 1 : -1;
                    break;
                }
            }
            if (seen) {
                break;
            }
            e--;
            if (e <= 0) {
                e = 0;
                break;
            }
        }
        steps = g_ai_steps[e];
        tseg = steps + s;
        if (tseg >= links) {
            tseg -= links;
        }

        /* How hard the way there is: the surface, the bends, the hill. */
        rough = (8.0 - g_surface_grip[(s8)cur->surface]) * 0.125;
        if (rough < 0.0) {
            rough = 0.0;
        }
        bends = 0.0;
        n = e + 1 > 5 ? 5 : e + 1;
        if (n > 0) {
            double w = (a->f38 * 0.4 + 0.6) / n, d;
            int j;

            for (j = 0; j < n; j++) {
                d = fabs((double)look[j]->hm - (double)look[j + 1]->hm);
                if (d > PI) {
                    d = fabs(d - TWO_PI);
                }
                bends += d * d * w * 1.6;
            }
        }
        t = (rough + 0.8) * bends + 1.0;
        dist = steps * 40.0;
        if (g_ai_lanes[tseg][lane] == NULL) {
            dy = (double)(cur->y1 - g_ai_lanes[tseg][0]->y1);
        } else {
            dy = (double)(cur->y1 - g_ai_lanes[tseg][lane]->y1);
        }
        slope = 1.0 + dy / dist * a->f40 * 1.1;
        if (slope < 1.0) {
            slope = 1.0;
        }
        if (1.0 < slope) {
            slope = sqrt(slope);
        }
        {
            double k1 = ((1.0 + a->skill) * 0.5 + 0.3) * slope;
            double top = a->f18 * 60.0 + 120.0, fall, r;

            speed = k1 / (a->f10 / (double)(e * 3 + 6) * 6.0 + 1.0) / ((t - 1.0) * 0.4 + 1.0);
            fall = top - c->f118;
            if (fall < 0.0) {
                fall = 0.0;
            }
            r = fall / top * a->f18 * 1.5 + 1.0;
            throttle = (speed * r + (r - 1.0) * 0.25) * 100.0;
            if (throttle > 100.0) {
                throttle = 100.0;
            }
        }

        /* The track's speed limits. */
        limit = -1.0;
        for (k = 0; k < 8; k++) {
            if (g_ai_limits[(track * 8 + k) * 2] == (seg & ~1)) {
                limit = g_ai_limits[(track * 8 + k) * 2 + 1];
            }
        }
        if (limit != -1.0 && limit != 500.0) {
            if (limit * 0.2777777777777778 < p->speed) {
                brake = (s32)((c->f118 - limit) * 0.5);
            }
            if (brake > 50) {
                brake = 50;
            }
            if (brake < 0) {
                brake = 0;
            }
        }
        if (limit == 500.0) {
            throttle = 100.0;
            if (difficulty < 1) {
                boost = 1;
            }
        }
        brake_amt = throttle * 1.5;
        if (brake_amt > 100.0) {
            brake_amt = 100.0;
        }

        /* Aim: the target less where the car is heading, a little
         * ahead of its slide unless it slides a lot. */
        aim = atan2(mz - c->z, mx - c->x);
        drift = wrap_once(c->f100 - c->heading);
        head = wrap_once(drift * 0.15 + c->heading);
        {
            double sl = fabs(c->heading - c->f100);

            if (sl > PI) {
                sl = -(sl - TWO_PI);
            }
            if (sl > 0.785399) {
                head = c->heading;
            }
        }
        aim -= head;
        if (aim > PI) {
            aim -= TWO_PI;
        }
        if (aim < -PI) {
            aim += TWO_PI;
        }
        road = wrap_once((double)cur->hm - head);
        steer = ((t - 1.0) * 0.25 + 1.0) * aim * 75.0;
        if (g_drivers[i].car == 5 || g_drivers[i].car == 8) {
            steer *= 1.2;
        }
        if (difficulty == 1) {
            steer *= 0.9;
        }
        if (difficulty == 2) {
            steer *= 0.8;
        }
        if (difficulty == 3) {
            steer *= 0.75;
        }
        if (steer > 100.0) {
            steer = 100.0;
        } else if (steer < -100.0) {
            steer = -100.0;
        }

        /* Around the nearest car, if it is ahead and near enough. */
        best = 100000.0;
        near_car = -1;
        for (k = 0; k < g_driver_count; k++) {
            if (k != i) {
                double dz = c->z - g_cars[k].z, dx = c->x - g_cars[k].x;
                double d = sqrt(dz * dz + dx * dx);

                if (d < best) {
                    near_car = k;
                    best = d;
                }
            }
        }
        if (near_car != -1) {
            const struct car *o = &g_cars[near_car];
            double ang = atan2(o->rear_z - c->front_z, o->rear_x - c->front_x) - head + 1.570798;
            double range, avoid;

            while (ang < -PI) {
                ang += TWO_PI;
            }
            while (ang > PI) {
                ang -= TWO_PI;
            }
            range = a->look * 800.0;
            if (ang > 1.270798 && ang < 1.870798 && range > best) {
                ang -= 1.570798;
                if (ang < -PI) {
                    ang += TWO_PI;
                }
                ang = 0.0 < ang || ang == 0.0 ? ang * -3.0 + 1.570798 : ang * -3.0 - 1.570798;
                avoid = (range - best) * (ang * -40.0) / range;
                if (avoid > 100.0) {
                    avoid = 100.0;
                } else if (avoid < -100.0) {
                    avoid = -100.0;
                }
                if (g_ai[near_car].side < -1.9 && avoid < 0.0) {
                    avoid = -avoid;
                } else if (g_ai[near_car].side > 1.9 && 0.0 < avoid) {
                    avoid = -avoid;
                }
                steer = (avoid - steer) / t + steer;
            }
        }

        /* Off the line on a straight: back toward the middle. */
        if (blocked == -1 && e > 2 && t < 2.0 && fabs(turn) < 0.3) {
            double want = (t - 1.0) * 50.0, div;

            if (want > 50.0) {
                want = 50.0;
            }
            div = 0.0 < a->side || a->side == 0.0 ? a->side * 2.5 : 2.5;
            steer = (want - steer) / div + steer;
        } else if (blocked == 1 && e > 2 && t < 2.0 && fabs(turn) < 0.3) {
            double want = (t - 1.0) * -50.0, div;

            if (want < -50.0) {
                want = -50.0;
            }
            div = 0.0 < a->side ? 2.5 : a->side * -2.5;
            steer = (want - steer) / div + steer;
        }
    }
    if (c->f558 != 0 && (double)c->f558 < difficulty * -9.0 + 32.4) {
        steer = -steer;
    }
    a->steer = steer;

    /* Stuck: turned across the road, or slow on the ground. */
    if (fabs(road) > 1.4 || (p->speed < 6.944444444444445 && c->f270 == 0)) {
        a->stuck += g_driver_count;
    } else if (!((double)a->stuck > 43.2)) {
        a->stuck = 0;
    } else if (g_track == 1 && s > 0x54 && s < 0x5f && a->side < 0.0) {
        a->stuck += g_driver_count;
    } else if (g_track == 4 && seg > 0x3d && seg < 0x41) {
        a->stuck += g_driver_count;
    } else if (g_track == 5 && s >= 0x92 && s <= 0x94) {
        a->stuck += g_driver_count;
    } else {
        a->stuck = 0;
    }
    if (!((double)a->stuck < 151.2)) {
        a->stuck = 0;
    }
    if ((double)a->stuck > 43.2 && (double)a->stuck < 97.2) {
        /* Backing out, turned the other way. */
        if ((double)a->stuck < 54.0) {
            a->steer = 0.0;
        } else {
            if (a->steer < 0.0 && a->steer > -70.0) {
                a->steer = -70.0;
            } else if (!(a->steer < 0.0) && a->steer < 70.0) {
                a->steer = 70.0;
            }
            if (g_track == 1 && s > 0x54 && s < 0x5f && a->side < 0.0) {
                a->steer = -100.0;
            } else if ((g_track == 4 && seg > 0x3d && seg < 0x41)
                       || (g_track == 5 && s >= 0x92 && s <= 0x94)) {
                a->steer = -100.0;
            }
        }
        race_control(0, (s32)brake_amt, (s32)-a->steer, 0);
    } else {
        if ((double)a->stuck > 97.2 && (double)a->stuck < 108.0) {
            a->steer = 0.0;
        }
        if (c->f270 != 0) {
            a->steer = 0.0;
        }
        if (c->f534 == 100.0 && c->f270 == 0 && brake == 0) {
            int go = 0;

            if (a->f20 * 0.13 + 1.07 > t && steps > 3 && fabs(a->steer) < a->f20 * 5.0 + 3.0) {
                go = 1;
            } else if (boost) {
                go = 1;
            }
            if (go && g_track == 4 && s > 0x28 && s < 0x36) {
                go = 0;
            }
            if (go && g_track == 0 && ((seg < -156 && seg > -162) || (seg < 0x66 && seg > 0x5a))) {
                go = 0;
            }
            if (go) {
                race_control((s32)throttle, 0, (s32)a->steer, 1);
                goto done;
            }
        }
        if (brake == 0) {
            race_control((s32)throttle, 0, (s32)a->steer, 0);
        } else {
            race_control(0, brake, (s32)a->steer, 0);
        }
    }
done:
    a->throttle = (s32)throttle;
    a->seg = s;
}
