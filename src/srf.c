/*
 * srf.c - what lies under a point of the track (0x412fc0..0x413380,
 * 0x427dc0..0x429093).
 *
 * The .SRF grid lists, a cell at a time, spans: the rows of a face of
 * an object, as an x range that moves along z. The scene's objects
 * hold the rest: meshes whose faces carry a surface type in their top
 * 16 bits.
 *
 * A mesh is s32 counts of vertices and faces, the vertices (x, y, z),
 * then faces of 11 dwords: the type, three vertex indices, ...
 */

#include <string.h>

#include "lisa/log.h"
#include "race.h"
#include "race_cars.h"
#include "race_init.h"
#include "scene.h"
#include "srf.h"

enum {
    SRF_PAIRS = 512,        /* unk_63a170 has no bound in the original */
    OBJ_CANDS = 8,          /* 0x4c5528 */
    WALL_EDGES = 3000,      /* 0x4c5b08 has no bound in the original */
    WALL_SEGS = 500         /* car.wall_segs */
};

/* A span of the grid, 24 bytes. */
struct srf_span {
    s32 x, z;               /* +0x00, where it starts */
    s32 slope_a, slope_b;   /* +0x08, 16.16 x per z of its two ends */
    s16 dz;                 /* +0x10, its length in z; < 0 for the second list */
    u16 face;               /* +0x12, dwords into the object's mesh */
    u32 obj;                /* +0x14, bytes into the object pool */
};

_Static_assert(sizeof(struct srf_span) == 24, "srf_span");

/* A face under the point: its object and the face in its mesh. */
struct srf_pair {
    struct scene_obj *obj;
    const s32 *face;
};

static struct srf_pair s_pairs[SRF_PAIRS];  /* unk_63a170 */
static struct srf_hit s_ground;             /* 0x4c53e0 */
static struct srf_hit s_obj_hit;            /* 0x4c5498 */
static struct srf_hit s_cands[OBJ_CANDS];   /* 0x4c5528 */
static struct srf_hit s_zone;               /* 0x4c57d0 */
/* An edge: 1.0 if another face shares it, then x1, z1, x2, z2. One more
 * than the edges, as the original's search reads one past the last. */
static double s_edges[WALL_EDGES + 1][5];   /* 0x4c5b08 */

/* imul and sar as the original does them. */
static s32 mul32(s32 a, s32 b)
{
    return (s32)((u32)a * (u32)b);
}

static s32 sar16(s32 v)
{
    return v >= 0 ? v >> 16 : ~(~v >> 16);
}

static s32 iabs(s32 v)
{
    return v < 0 ? -v : v;
}

static const s32 *mesh_vertex(const s32 *mesh, s32 i)
{
    return mesh + 2 + 3 * i;
}

static const s32 *mesh_faces(const s32 *mesh)
{
    return mesh + 2 + 3 * mesh[0];
}

static const struct srf_span *span_at(const u32 *list, u32 i)
{
    return (const struct srf_span *)(g_srf.spans + list[i]);
}

/* 0x446578 (dz >= 0) and 0x4465e1 (dz <= 0), in asm in the original:
 * the spans of a list that cover (x, z). */
static int spans_hit(const u32 *list, u32 count, s32 x, s32 z, int up, int n)
{
    u32 i;

    for (i = 0; i < count; i++) {
        const struct srf_span *s = span_at(list, i);
        s32 t;
        struct scene_obj *o;

        if (up ? (z < s->z || s->z + s->dz < z) : (z > s->z || s->z + s->dz > z)) {
            continue;
        }
        t = z - s->z;
        if (sar16(mul32(t, s->slope_a)) + s->x > x || sar16(mul32(t, s->slope_b)) + s->x < x) {
            continue;
        }
        if (n == SRF_PAIRS) {
            log_printf("srf: more than %d faces under a point\n", SRF_PAIRS);
            return n;
        }
        if (s->obj % 0x2a != 0) {
            log_printf("srf: object offset %u is not an object\n", (unsigned)s->obj);
            continue;
        }
        o = scene_obj_at((int)(s->obj / 0x2a));
        s_pairs[n].obj = o;
        s_pairs[n].face = o->mesh + s->face;
        n++;
    }
    return n;
}

/* 0x413380: the height of a face's centre; x and z are not used. */
static s32 face_height(const struct srf_pair *p, s32 x, s32 z)
{
    const s32 *mesh = p->obj->mesh;
    s32 sum = mesh_vertex(mesh, p->face[1])[1] + mesh_vertex(mesh, p->face[2])[1]
              + mesh_vertex(mesh, p->face[3])[1];

    return sum / -3 + p->obj->y;
}

static int off_track(s32 type)
{
    return type >= 40 && type <= 79;
}

/* 0x412fc0
 * Of the faces under (x, z), the one nearest height y (the distance is
 * squared above it but not below); anything beats off-track ground
 * (types 40..79). */
struct srf_hit *srf_lookup(s32 x, s32 y, s32 z)
{
    s32 cell = (z / g_srf.cell_h) * g_srf.gw + x / g_srf.cell_w;
    const struct srf_cell *c;
    const struct srf_pair *best = &s_pairs[0];
    const s32 *mesh;
    const s32 *v[3];
    s32 best_type = 0x4f, best_d = 1000000000;
    s32 a, b, cz, d, e, f;
    int n, i, k;

    /* The original reads outside the grid here. */
    if (x < 0 || z < 0 || x / g_srf.cell_w >= g_srf.gw || cell >= g_srf.gw * g_srf.gh) {
        s_ground.type = -1;
        return &s_ground;
    }
    c = &g_srf.cells[cell];
    n = spans_hit((const u32 *)((const u8 *)g_srf.list_b + c->list_b), c->count_b, x, z, 1, 0);
    n = spans_hit((const u32 *)((const u8 *)g_srf.list_a + c->list_a), c->count_a, x, z, 0, n);
    if (n == 0) {
        s_ground.type = -1;
        return &s_ground;
    }
    if (n > 1) {
        for (i = 0; i < n; i++) {
            s32 dist = face_height(&s_pairs[i], x, z) - y;
            s32 type = sar16(s_pairs[i].face[0]);
            int take;

            dist = dist < 0 ? -dist : mul32(dist, dist);
            if (best_d > dist) {
                take = !off_track(type) || off_track(best_type);
            } else {
                take = !off_track(type) && off_track(best_type);
            }
            if (take) {
                best_type = type;
                best_d = dist;
                best = &s_pairs[i];
            }
        }
    }
    mesh = best->obj->mesh;
    for (k = 0; k < 3; k++) {
        v[k] = mesh_vertex(mesh, best->face[1 + k]);
    }
    s_ground.type = sar16(best->face[0]);
    a = v[0][0] - v[1][0];
    b = v[0][1] - v[1][1];
    cz = v[0][2] - v[1][2];
    d = v[2][0] - v[1][0];
    e = v[2][1] - v[1][1];
    f = v[2][2] - v[1][2];
    s_ground.normal[0] = mul32(b, f) - mul32(cz, e);
    s_ground.normal[1] = mul32(cz, d) - mul32(a, f);
    s_ground.normal[2] = mul32(a, e) - mul32(b, d);
    for (k = 0; k < 3; k++) {
        s_ground.v[k][0] = best->obj->x + v[k][0];
        s_ground.v[k][1] = v[k][1] - best->obj->y;
        s_ground.v[k][2] = best->obj->z + v[k][2];
    }
    s_ground.obj = scene_obj_index(best->obj);
    s_ground.f20 = best->obj->f20;
    s_ground.klass = best->obj->klass;
    return &s_ground;
}

/* Whether (px, pz) is inside the triangle, by counting the crossings of
 * its edges to the +x side; divisions truncate as idiv does. */
static int in_triangle(const s32 vx[3], const s32 vz[3], s32 px, s32 pz)
{
    int i, j, in = 0;

    for (i = 0, j = 2; i < 3; j = i++) {
        if ((vz[i] <= pz && vz[j] > pz) || (vz[j] <= pz && vz[i] > pz)) {
            s32 cx = vx[i] + mul32(vx[j] - vx[i], pz - vz[i]) / (vz[j] - vz[i]);

            if (cx > px) {
                in = !in;
            }
        }
    }
    return in;
}

/* A face's corners in its mesh. */
static void face_corners(const s32 *mesh, const s32 *face, s32 vx[3], s32 vy[3], s32 vz[3])
{
    int k;

    for (k = 0; k < 3; k++) {
        const s32 *v = mesh_vertex(mesh, face[1 + k]);

        vx[k] = v[0];
        vy[k] = v[1];
        vz[k] = v[2];
    }
}

/* A trigger's record: the object's position and the face as it is in
 * the mesh. */
static void zone_record(struct srf_hit *h, const struct scene_obj *o,
                        const s32 vx[3], const s32 vy[3], const s32 vz[3])
{
    int k;

    h->type = o->klass;
    h->normal[0] = o->x;
    h->normal[1] = o->y;
    h->normal[2] = o->z;
    for (k = 0; k < 3; k++) {
        h->v[k][0] = vx[k];
        h->v[k][1] = vy[k];
        h->v[k][2] = vz[k];
    }
    h->obj = scene_obj_index(o);
    h->f20 = o->f20;
    h->klass = o->klass;
}

/* 0x427dc0
 * Over the visible objects: the faces of solid objects (class < 100)
 * under (x, z), with their centre within 200, that are not off-track
 * (types 40..79), and the triggers (classes 300..349) within 190 of y.
 * A trigger wins; otherwise the face whose centre is the least height
 * above y + y_ofs, given as world corners and a normal.
 *
 * Triggers are only looked at while the last call found something
 * (0..349): the original reads its own last record. With slot > 0 the
 * original also lists the faces in the car's f3dc / f47c arrays as
 * addresses; the race passes -1, and the port does not keep them. */
struct srf_hit *race_obj_query(s32 x, s32 y, s32 z, s32 slot, s32 y_ofs)
{
    const struct car *car = &g_cars[r_5285c0];
    s32 range = car->deck != 0 ? 900 : 600;
    int i, ncand = 0, trigger = 0;

    if (slot > 0) {
        log_printf("race_obj_query: slot %d is not kept (0x427dc0)\n", (int)slot);
    }
    for (i = 0; i < g_scene->obj_count; i++) {
        const struct scene_obj *o = g_scene_visible[i];
        const s32 *mesh = o->mesh, *face = mesh_faces(mesh);
        s32 dx = x - o->x, dy = y - o->y, dz = z - o->z;
        s32 vx[3], vy[3], vz[3];
        int f;

        if (o->klass < 100) {
            if (iabs(dx) >= range || iabs(dz) >= range) {
                continue;
            }
            for (f = 0; f < mesh[1]; f++, face += 11) {
                u32 t;
                struct srf_hit *h;
                int k;

                face_corners(mesh, face, vx, vy, vz);
                if (iabs((vx[1] + vx[2] + vx[0]) / 3 - dx) >= 200
                    || iabs((vz[1] + vz[2] + vz[0]) / 3 - dz) >= 200) {
                    continue;
                }
                if (!in_triangle(vx, vz, dx, dz)) {
                    continue;
                }
                t = (u32)face[0] & 0xffff0000u;
                if (t >= 0x280000u && t <= 0x4f0000u) {
                    continue;
                }
                if (ncand == OBJ_CANDS) {
                    log_printf("N");
                    continue;
                }
                h = &s_cands[ncand++];
                h->type = (s32)((u32)face[0] >> 16);
                h->normal[0] = o->x;
                h->normal[1] = o->y;
                h->normal[2] = o->z;
                for (k = 0; k < 3; k++) {
                    h->v[k][0] = vx[k];
                    h->v[k][1] = vy[k];
                    h->v[k][2] = vz[k];
                }
                h->obj = scene_obj_index(o);
                h->f20 = o->f20;
                h->klass = o->klass;
            }
        } else if (o->klass >= 300 && o->klass < 350) {
            if (s_obj_hit.type >= 350 || s_obj_hit.type < 0) {
                continue;
            }
            if (iabs(dx) >= range || iabs(dz) >= range || iabs(dy) >= 190) {
                continue;
            }
            for (f = 0; f < mesh[1]; f++, face += 11) {
                face_corners(mesh, face, vx, vy, vz);
                if (in_triangle(vx, vz, dx, dz)) {
                    trigger = 1;
                    zone_record(&s_obj_hit, o, vx, vy, vz);
                    break;
                }
            }
        }
    }
    if (ncand > 0 && !trigger) {
        const struct srf_hit *c;
        s32 best = 1000000, a, b, cz, d, e, f;
        int pick = 0, k;

        for (i = 0; i < ncand; i++) {
            s32 dist;

            c = &s_cands[i];
            dist = (c->v[1][1] - 3 * c->normal[1] + c->v[0][1] + c->v[2][1]) / 3 + y_ofs + y;
            if (dist < best && dist >= 0
                && mul32(c->v[2][2] - c->v[1][2], c->v[0][0] - c->v[1][0])
                   != mul32(c->v[0][2] - c->v[1][2], c->v[2][0] - c->v[1][0])) {
                best = dist;
                pick = i;
            }
        }
        c = &s_cands[pick];
        a = c->v[0][0] - c->v[1][0];
        b = c->v[0][1] - c->v[1][1];
        cz = c->v[0][2] - c->v[1][2];
        d = c->v[2][0] - c->v[1][0];
        e = c->v[2][1] - c->v[1][1];
        f = c->v[2][2] - c->v[1][2];
        if (mul32(f, a) == mul32(cz, d) || best >= 1000000) {
            s_obj_hit.type = -1;
            return &s_obj_hit;
        }
        s_obj_hit.type = c->type;
        s_obj_hit.normal[0] = mul32(f, b) - mul32(e, cz);
        s_obj_hit.normal[1] = mul32(cz, d) - mul32(f, a);
        s_obj_hit.normal[2] = mul32(e, a) - mul32(b, d);
        for (k = 0; k < 3; k++) {
            s_obj_hit.v[k][0] = c->v[k][0] + c->normal[0];
            s_obj_hit.v[k][1] = c->v[k][1] - c->normal[1];
            s_obj_hit.v[k][2] = c->v[k][2] + c->normal[2];
        }
        s_obj_hit.obj = c->obj;
        s_obj_hit.f20 = c->f20;
        s_obj_hit.klass = c->klass;
    } else if (!trigger) {
        s_obj_hit.type = -1;
    }
    return &s_obj_hit;
}

/* 0x428730
 * Points g_scene's camera down at the car (500 above it, its race
 * camera's focal lengths), gathers the cells there and finds the
 * trigger zone (classes 300..349) under (x, z); the last one found
 * wins. Once any zone was within 190 of y, those further away count
 * too, as in the original. */
struct srf_hit *race_zone_query(s32 x, s32 y, s32 z, s32 car)
{
    struct scene *sc = g_scene;
    const struct car *c = &g_cars[car];
    const struct race_cam *cam = &g_race_cams[car];
    s32 range = c->deck != 0 ? 900 : 600;
    int i, found = 0, near_y = 0;

    sc->stage_lookat = 0;
    sc->x = c->x;
    sc->y = c->y + 500.0;
    sc->z = c->z;
    sc->pitch = 850.0;
    sc->yaw = 0.0;
    sc->roll = 0.0;
    sc->target_roll = 0;
    sc->focal_x = cam->f58;
    sc->focal_y = cam->f5c;
    sc->near_ofs = (s32)cam->f60;
    scene_gather_view();
    for (i = 0; i < sc->obj_count; i++) {
        const struct scene_obj *o = g_scene_visible[i];
        const s32 *mesh = o->mesh, *face = mesh_faces(mesh);
        s32 dx = x - o->x, dy = y - o->y, dz = z - o->z;
        s32 vx[3], vy[3], vz[3];
        int f;

        if (o->klass < 300 || o->klass >= 350) {
            continue;
        }
        if (iabs(dy) < 190) {
            near_y = 1;
        }
        if (iabs(dx) >= range || iabs(dz) >= range || !near_y) {
            continue;
        }
        for (f = 0; f < mesh[1]; f++, face += 11) {
            face_corners(mesh, face, vx, vy, vz);
            if (in_triangle(vx, vz, dx, dz)) {
                found = 1;
                zone_record(&s_zone, o, vx, vy, vz);
                break;
            }
        }
    }
    if (!found) {
        s_zone.type = -1;
    }
    return &s_zone;
}

/* 0x428b90
 * The faces of the solid objects near (x, z) and within a height of -y
 * (by track; more on a deck) that can be driven on: types below 40, 100
 * and 101, and on a deck 110..131. Their edges that no other face
 * shares, the other way round within 16, are the walls. */
s32 race_wall_edges(double *out, s32 x, s32 y, s32 z, s32 deck)
{
    s32 range, height = 0, n = 0, count = 0;
    int i, j;

    if (deck == 0) {
        range = 600;
        switch (g_track) {
        case 0: case 1: case 4: case 5:
            height = 350;
            break;
        case 2:
            height = 200;
            break;
        case 3:
            height = 450;
            break;
        case 6:
            height = 125;
            break;
        }
    } else {
        range = 700;
        height = 2500;
    }
    for (i = 0; i < g_scene->obj_count; i++) {
        const struct scene_obj *o = g_scene_visible[i];
        const s32 *mesh = o->mesh, *face = mesh_faces(mesh);
        int f;

        if (iabs(x - o->x) >= range || iabs(o->y + y) >= height || iabs(z - o->z) >= range
            || o->klass >= 100) {
            continue;
        }
        for (f = 0; f < mesh[1]; f++, face += 11) {
            u32 type = (u32)face[0] >> 16;
            const s32 *v[3];
            int k;

            if (!(type < 40 || type == 100 || type == 101
                  || (type >= 110 && type <= 131 && deck == 1))) {
                continue;
            }
            if (n + 3 > WALL_EDGES) {
                log_printf("race_wall_edges: more than %d edges\n", WALL_EDGES);
                break;
            }
            for (k = 0; k < 3; k++) {
                v[k] = mesh_vertex(mesh, face[1 + k]);
            }
            for (k = 0; k < 3; k++) {
                double *e = s_edges[n + k];

                e[0] = 0.0;
                e[1] = (double)(v[k][0] + o->x);
                e[2] = (double)(v[k][2] + o->z);
                e[3] = (double)(v[(k + 1) % 3][0] + o->x);
                e[4] = (double)(v[(k + 1) % 3][2] + o->z);
            }
            n += 3;
        }
    }
    for (i = 0; i < n; i++) {
        double *a = s_edges[i];

        if (a[0] != 0.0) {
            continue;
        }
        for (j = i + 1; j <= n; j++) {
            double *b = s_edges[j];

            if (iabs((s32)(a[1] - b[3])) < 16 && iabs((s32)(a[2] - b[4])) < 16
                && iabs((s32)(a[3] - b[1])) < 16 && iabs((s32)(a[4] - b[2])) < 16) {
                a[0] = 1.0;
                b[0] = 1.0;
                break;
            }
        }
        if (a[0] != 0.0) {
            continue;
        }
        if (count == WALL_SEGS) {
            log_printf("race_wall_edges: more than %d walls\n", WALL_SEGS);
            break;
        }
        out[4 * count] = a[1];
        out[4 * count + 1] = a[2];
        out[4 * count + 2] = a[3];
        out[4 * count + 3] = a[4];
        count++;
    }
    return count;
}
