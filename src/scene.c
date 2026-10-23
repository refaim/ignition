/*
 * scene.c - the 3D engine's scene, objects and grid.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "race_data.h"
#include "scene.h"

struct scene *g_scene;                      /* 0x63c5f0 */
struct scene_obj **g_scene_visible;         /* 0x63c5cc */
u8 *g_scene_textures[SCENE_TEXTURES];       /* 0x63c5c8 */
int g_scene_texture_pages[SCENE_TEXTURES];

static float *s_sin;                        /* 0x63b5f4, from -180 degrees */
static struct scene_obj **s_grid;           /* 0x63b5ec */
static int s_grid_w;                        /* 0x63c5b8, in cells */
static int s_world_w, s_world_h;            /* 0x63c608, 0x63c60c */
static struct scene_obj *s_objs;            /* 0x63c5dc */
static int s_obj_total;
static struct scene_obj **s_free_list;           /* 0x63c5c0, ended by -1 */
static struct scene_obj **s_free_at;      /* 0x63c5e8 */

#define FREE_END ((struct scene_obj *)(intptr_t)-1)

/* sin of a tenth of a degree, a from -1800 to 6299. */
static double tsin(int a)
{
    return s_sin[1800 + a];
}

static double tcos(int a)
{
    return s_sin[1800 + 900 + a];
}

/* 0x4468d0
 * The original also allocates the pipeline's buffers: the visible list,
 * the projected vertices, the depth buckets and the polygon store. Only
 * the visible list is needed here. */
struct scene *scene_create(void)
{
    struct scene *sc;
    int i;

    g_scene_visible = calloc(1500, sizeof(*g_scene_visible));
    sc = calloc(1, sizeof(*sc));
    s_sin = calloc(8100, sizeof(*s_sin));
    g_scene = sc;
    if (g_scene_visible == NULL || sc == NULL || s_sin == NULL) {
        return NULL;
    }
    sc->stage_lookat = 1;
    sc->stage_gather = 1;
    sc->stage_transform = 1;
    sc->f44 = sc->f48 = sc->f4c = 1;
    sc->stage_emit = 1;
    sc->stage_collect = 1;
    sc->perspective = 1;
    sc->f8c = sc->f90 = sc->f94 = 1;
    sc->focal_x = 253;
    sc->focal_y = 207;
    sc->near_ofs = 44;
    sc->f98 = 10;
    sc->zoom = 1.0;
    sc->center_x = 160;
    sc->center_y = 100;
    for (i = -1800; i < 6300; i++) {
        s_sin[1800 + i] = (float)sin(i * 0.1 * 0.017453292519943334);
    }
    return sc;
}

/* 0x446c30 */
void scene_free(void)
{
    free(g_scene_visible);
    free(g_scene);
    free(s_sin);
    free(s_grid);
    free(s_free_list);
    free(s_objs);
    g_scene_visible = NULL;
    g_scene = NULL;
    s_sin = NULL;
    s_grid = NULL;
    s_free_list = NULL;
    s_objs = NULL;
    s_obj_total = 0;
}

/* 0x446ca0: a world of w x h units in cells of 256, and room for
 * `count` objects. */
int raster_set_grid(int w, int h, int count)
{
    int i;

    s_world_w = w;
    s_world_h = h;
    s_grid_w = (w + 0xff) / 256;
    free(s_grid);
    free(s_free_list);
    free(s_objs);
    s_grid = calloc((size_t)(s_grid_w * ((h + 0xff) / 256)), sizeof(*s_grid));
    s_free_list = calloc((size_t)count + 5, sizeof(*s_free_list));
    s_objs = calloc((size_t)count, sizeof(*s_objs));
    s_free_at = s_free_list;
    if (s_grid == NULL || s_free_list == NULL || s_objs == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        s_free_list[i] = &s_objs[i];
    }
    s_free_list[count] = FREE_END;
    s_obj_total = count;
    return 0;
}

int raster_set_screen(int w, int h, int flags)
{
    (void)w;
    (void)h;
    (void)flags;
    return 1;
}

/* 0x4471e0: the focal lengths, the eye's distance behind the screen and
 * the centre for a w x h view. */
void scene_set_screen(int w, int h)
{
    struct scene *sc = g_scene;

    sc->focal_x = (int)(w * 0.003125 * 253.0);
    sc->focal_y = (int)(h * 0.005 * 207.45999999999998);
    sc->near_ofs = (int)((w * 0.003125 + h * 0.004166666666666667) * 0.3333333333333333 * 40.0);
    sc->center_x = (int)(w * 0.5);
    sc->center_y = (int)(h * 0.5);
}

/* The largest distance of a mesh's vertices from its origin, at least
 * -1000; the original skips vertices whose |x|+|y|+|z| is no more than
 * the largest yet. */
static s16 mesh_radius(const s32 *mesh)
{
    s32 most = -1000;
    const s32 *v = mesh + 2;
    int i;

    for (i = 0; i < mesh[0]; i++, v += 3) {
        s32 l1 = abs(v[0]) + abs(v[1]) + abs(v[2]);

        if (l1 > most) {
            s32 r = (s32)sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);

            if (r > most) {
                most = r;
            }
        }
    }
    return (s16)most;
}

static void set_mesh(struct scene_obj *o, const s32 *mesh, int tex, int klass,
                     int f20, int radius, int group)
{
    o->mesh = mesh;
    o->tex = tex;
    o->klass = (s16)klass;
    o->f20 = (s16)f20;
    o->radius = (s16)radius;
    o->group = (s16)group;
    if ((s16)radius == -1) {
        o->radius = mesh_radius(mesh);
    }
}

/* 0x446d90: takes an object from the pool for `desc`; it is not in the
 * grid until linked. */
int scene_obj_create(struct scene_desc *desc, const s32 *mesh, int tex, int klass,
                     int f20, int radius, int group)
{
    struct scene_obj *o = *s_free_at;

    if (o == FREE_END) {
        return -1;
    }
    s_free_at++;
    o->self = o;
    o->x = desc->x;
    o->y = desc->y;
    o->z = desc->z;
    o->ax = (s16)desc->ax;
    o->ay = (s16)desc->ay;
    o->az = (s16)desc->az;
    o->next = NULL;
    set_mesh(o, mesh, tex, klass, f20, radius, group);
    desc->obj = o;
    desc->in_grid = 0;
    return 0;
}

static struct scene_obj **cell(s32 x, s32 z)
{
    return &s_grid[(z / 256) * s_grid_w + x / 256];
}

/* 0x446eb0 */
int scene_obj_link(struct scene_desc *desc)
{
    struct scene_obj *o = desc->obj, **c;

    if (desc->in_grid == 1) {
        return 0;
    }
    o->ax = (s16)desc->ax;
    o->ay = (s16)desc->ay;
    o->az = (s16)desc->az;
    o->x = desc->x;
    o->y = desc->y;
    o->z = desc->z;
    c = cell(o->x, o->z);
    desc->in_grid = 1;
    o->next = *c;
    *c = o;
    return 0;
}

static void cell_remove(struct scene_obj **c, struct scene_obj *o)
{
    struct scene_obj *p;

    if (*c == o) {
        *c = o->next;
        return;
    }
    for (p = *c; p != NULL && p->next != o; p = p->next) {
    }
    if (p != NULL) {
        p->next = o->next;
    }
}

/* 0x446f30: moves an object to `desc`'s place and angles, changing its
 * cell if it left it. Returns -1 for a place outside the world. */
int scene_obj_move(struct scene_desc *desc)
{
    struct scene_obj *o = desc->obj;
    s32 old_x = o->x, old_z = o->z;

    if (desc->x < 0 || desc->z < 0 || s_world_w < desc->x || s_world_h < desc->z) {
        return -1;
    }
    o->x = desc->x;
    o->y = desc->y;
    o->z = desc->z;
    o->ax = (s16)desc->ax;
    o->ay = (s16)desc->ay;
    o->az = (s16)desc->az;
    if (desc->in_grid == 1
        && (desc->x / 256 != old_x / 256 || desc->z / 256 != old_z / 256)) {
        cell_remove(cell(old_x, old_z), o);
        o->next = *cell(o->x, o->z);
        *cell(o->x, o->z) = o;
    }
    return 0;
}

/* 0x447150 */
int scene_obj_unlink(struct scene_desc *desc)
{
    struct scene_obj *o = desc->obj;

    if (desc->in_grid == 1) {
        cell_remove(cell(o->x, o->z), o);
        desc->in_grid = 0;
    }
    return 0;
}

/* 0x447070 */
int scene_obj_set_mesh(struct scene_desc *desc, const s32 *mesh, int tex, int klass,
                       int f20, int radius, int group)
{
    set_mesh(desc->obj, mesh, tex, klass, f20, radius, group);
    return 0;
}

static double wrap3600(double a)
{
    while (a < 0.0) {
        a += 3600.0;
    }
    while (a >= 3600.0) {
        a -= 3600.0;
    }
    return a;
}

/* 0x448990: aims the camera at its target. The original works out the
 * quadrants by hand; atan2 gives the same angles. */
void scene_look_at(struct scene *sc)
{
    double dx = sc->target_x - sc->x;
    double dy = sc->y - sc->target_y;
    double dz = sc->target_z - sc->z;
    double d = sqrt(dx * dx + dz * dz);

    sc->pitch = wrap3600(atan2(dy, d) * 1800.0 / M_PI);
    sc->yaw = wrap3600(atan2(dx, dz) * 1800.0 / M_PI);
    sc->roll = sc->target_roll;
}

/* 0x449e70: the camera's rows, from the negated angles. */
void scene_view_rows(const struct scene *sc, double m[3][3])
{
    const double k = -M_PI / 1800.0;
    double sp = sin(sc->pitch * k), cp = cos(sc->pitch * k);
    double sy = sin(sc->yaw * k), cy = cos(sc->yaw * k);
    double sr = sin(sc->roll * k), cr = cos(sc->roll * k);

    m[0][0] = cr * cy - sp * sy * sr;
    m[0][1] = -cp * sr;
    m[0][2] = cr * sy + sp * cy * sr;
    m[1][0] = cy * sr + cr * sp * sy;
    m[1][1] = cr * cp;
    m[1][2] = sr * sy - cr * sp * cy;
    m[2][0] = -cp * sy;
    m[2][1] = sp;
    m[2][2] = cp * cy;
}

/* 0x44a900: Ry(-ay) Rx(ax) Rz(-az) diag(1, -1, 1), from the sine
 * table, the angles wrapped to 0..3599. */
void scene_obj_rows(const struct scene_obj *o, double m[3][3])
{
    int ax = ((o->ax % 3600) + 3600) % 3600;
    int ay = ((o->ay % 3600) + 3600) % 3600;
    int az = ((o->az % 3600) + 3600) % 3600;
    double sx = tsin(ax), cx = tcos(ax);
    double sy = -tsin(ay), cy = tcos(ay);
    double sz = -tsin(az), cz = tcos(az);
    double ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    double rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
    double rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
    double t[3][3];
    int i, j, k;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            t[i][j] = 0;
            for (k = 0; k < 3; k++) {
                t[i][j] += ry[i][k] * rx[k][j];
            }
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            m[i][j] = 0;
            for (k = 0; k < 3; k++) {
                m[i][j] += t[i][k] * rz[k][j];
            }
        }
        m[i][1] = -m[i][1];
    }
}

/* Stage 2 of the frame (0x448e70), without its sphere cull: the
 * objects in the square of cells `radius` around the camera's cell,
 * the cell lists checked as the original checks them. */
int scene_gather(const struct scene *sc, int radius, struct scene_obj **out, int max)
{
    int gh, cx, cz, x, z, n = 0;

    if (s_grid == NULL) {
        return 0;
    }
    gh = (s_world_h + 0xff) / 256;
    cx = (int)sc->x / 256;
    cz = (int)sc->z / 256;
    for (z = cz - radius; z <= cz + radius; z++) {
        if (z < 0 || z >= gh) {
            continue;
        }
        for (x = cx - radius; x <= cx + radius; x++) {
            struct scene_obj *o;

            if (x < 0 || x >= s_grid_w) {
                continue;
            }
            o = s_grid[z * s_grid_w + x];
            if (o == NULL || o->self != o) {
                continue;
            }
            for (; o != NULL && n < max; o = o->next) {
                out[n++] = o;
            }
        }
    }
    return n;
}

s32 g_scene_group;                          /* 0x63c5d8 */

/* The original keeps an object's address where the port keeps its
 * index; the .SRF names objects by their offset into the pool. */
int scene_obj_index(const struct scene_obj *o)
{
    return (int)(o - s_objs);
}

/* The pool, as 0x63c5c0 walks it: the objects in use have a mesh. */
struct scene_obj *scene_obj_pool(int *count)
{
    *count = s_obj_total;
    return s_objs;
}

struct scene_obj *scene_obj_at(int index)
{
    return &s_objs[index];
}

/* 0x448c30
 * Gathers the objects of the cells in a disc ahead of the camera (where
 * by its pitch and yaw) into the visible list. With g_scene_group set
 * only objects of no group or that group come in. The original walks
 * past the grid's ends into other memory; the port skips such cells. */
int scene_gather_view(void)
{
    struct scene *sc = g_scene;
    int sector, start, row, cells, k, n = 0;
    const s32 *ahead;

    sc->obj_count = 0;
    sector = (int)(sc->pitch * 0.01) * 36 + (int)(sc->yaw * 0.01);
    if (sector < 0 || sector >= 36 * 36) {
        sector = 0;
    }
    ahead = &g_cells_ahead[2 * sector];
    cells = s_grid_w * ((s_world_h + 0xff) / 256);
    start = ((int)sc->z / 256 + ahead[1] + g_cells_disc[1]) * s_grid_w
            + (int)sc->x / 256 + ahead[0] + g_cells_disc[2];
    /* Rows of (cells, step to the next row); the last step is -5000. */
    for (row = 3;; row += 2) {
        for (k = 0; k < g_cells_disc[row]; k++) {
            struct scene_obj *o;

            start++;
            if (s_grid == NULL || start < 0 || start >= cells) {
                continue;
            }
            o = s_grid[start];
            if (o == NULL || o->self != o) {
                continue;
            }
            for (; o != NULL; o = o->next) {
                if (o->self != o || n >= 1500) {
                    continue;
                }
                if (g_scene_group != 0 && o->group != 0 && o->group != g_scene_group) {
                    continue;
                }
                g_scene_visible[n++] = o;
            }
        }
        if (g_cells_disc[row + 1] == -5000) {
            break;
        }
        start += g_cells_disc[row + 1] + s_grid_w;
    }
    sc->obj_count = n;
    sc->obj_count_all = n;
    return 0;
}
