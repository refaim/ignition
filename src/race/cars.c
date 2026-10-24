/*
 * race/cars.c - the race's cars: their models, wheels, curves and
 * starting state (0x41d190, 0x4196a0, 0x411dc0).
 *
 * CARS.PLC places each car model's body (type 10 k) and its four
 * wheels (10 k + 1 .. 10 k + 4) in CARS.MSH.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/log.h"
#include "lisa/mem.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/data.h"
#include "startup.h"

enum {
    CAR_MODELS = 11,
    WHEEL_DWORDS = 0x208       /* room for a wheel mesh's copy */
};

struct car *g_cars;                         /* 0x5daffc */
struct car_phys *g_car_phys;                /* 0x563d54 */
struct wheel_info *g_wheel_info;            /* 0x563da4, 4 a model */
s32 *g_car_meshes[CAR_MODELS];              /* 0x5db000 */
s32 *g_wheel_meshes[CAR_MODELS * 4];        /* 0x552e60 */
s32 g_wheel_dx[CAR_MODELS * 4];             /* 0x5533b0 */
s32 g_wheel_dz[CAR_MODELS * 4];             /* 0x553300 */
s16 g_car_curves_built[CAR_MODELS][3][200]; /* 0x54f9f0 */
s32 g_driver_wheels[RACE_DRIVERS][4][WHEEL_DWORDS];    /* 0x5537d0 */
struct car_hull g_car_hulls[RACE_DRIVERS];  /* 0x603710 */
s32 g_car7[RACE_DRIVERS][0x32];             /* 0x527f80, car 7 only */
s32 r_5530bc, r_5530c0, r_5530c4;           /* the first car's place */

/* 0x446330 */
double angle_wrap(double a)
{
    while (a < 0.0) {
        a += 6.2831853071796;
    }
    while (a >= 6.2831853071796) {
        a -= 6.2831853071796;
    }
    return a;
}

/* 0x411dc0
 * A curve through four points (x0, y0) .. (x3, y3), sampled at each
 * whole x from x0: three cubic pieces with slope 1.2 at the start, 0 at
 * the end, and between the slope of the neighbours' chord. Samples are
 * truncated to s16; the last is y3. */
static void curve_build(const s32 p[8], s16 *out)
{
    s32 x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
    s32 l1 = x1 - x0, l2 = x2 - x1, l3 = x3 - x2;
    double s1 = (double)(y2 - y0) / (x2 - x0);
    double s2 = (double)(y3 - y1) / (x3 - x1);
    double a1 = ((s1 + 1.2000000476837158) * l1 + 2 * (y0 - y1)) / ((double)l1 * l1 * l1);
    double b1 = (3 * (y1 - y0) - (s1 + 2.4000000953674316) * l1) / ((double)l1 * l1);
    double a2 = ((s1 + s2) * l2 + 2 * (y1 - y2)) / ((double)l2 * l2 * l2);
    double b2 = (3 * (y2 - y1) - (s1 * 2.0 + s2) * l2) / ((double)l2 * l2);
    double a3 = (s2 * l3 + 2 * (y2 - y3)) / ((double)l3 * l3 * l3);
    double b3 = (3 * (y3 - y2) - s2 * 2.0 * l3) / ((double)l3 * l3);
    int n = 0, i;

    for (i = 0; i < l1; i++) {
        out[n++] = (s16)(s32)(((a1 * i + b1) * i + 1.2000000476837158) * i + y0);
    }
    for (i = 0; i < l2; i++) {
        out[n++] = (s16)(s32)(((a2 * i + b2) * i + s1) * i + y1);
    }
    for (i = 0; i < l3; i++) {
        out[n++] = (s16)(s32)(((a3 * i + b3) * i + s2) * i + y2);
    }
    out[n] = (s16)y3;
}

/* 0x4196a0
 * The computer's drivers take cars from the difficulty's row of
 * g_ai_car_order in turn, skipping the players' cars, and their cars'
 * codes as names. */
static void pick_ai_cars(void)
{
    s32 humans[RACE_DRIVERS];
    const s32 *row = g_ai_car_order + 10 * g_mirror;
    int n_humans = 0, next = 0, alt = 5, i, k;

    for (i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].control == 0 || g_drivers[i].control == 2) {
            humans[n_humans++] = g_drivers[i].car;
        }
    }
    for (i = 0; i < g_driver_count; i++) {
        struct race_driver *d = &g_drivers[i];
        int free_car = 1;

        if (d->control != 1) {
            continue;
        }
        for (k = 0; k < n_humans; k++) {
            if (humans[k] == row[next]) {
                free_car = 0;
            }
        }
        if (free_car) {
            d->car = row[next];
        } else {
            for (k = 0; k < n_humans; k++) {
                if (humans[k] == row[alt]) {
                    alt++;
                    k = -1;
                }
            }
            d->car = row[alt++];
        }
        next++;
    }
    for (i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].control == 1) {
            snprintf(g_drivers[i].name, sizeof(g_drivers[i].name), "%.4s",
                     g_car_codes[g_drivers[i].car]);
        }
    }
    for (i = 0; i < g_driver_count; i++) {
        struct race_driver *d = &g_drivers[i];

        if (d->control == 1) {
            g_cars[i].ai_skill = g_car_499880[g_car_499a90[d->car] + 11 * g_mirror];
            g_ai_params[d->car * 9 + 5] = g_mirror * 0.19 + 0.4;
        } else {
            g_cars[i].ai_skill = 0;
        }
    }
}

static s32 mesh_dwords(const s32 *m)
{
    return m[0] * 3 + m[1] * 11 + 2;
}

/* The body and wheels of each car model, their curves. */
static void load_models(s32 rear[CAR_MODELS], s32 front[CAR_MODELS])
{
    int k, w, i, found_any = 0;

    for (k = 0; k < CAR_MODELS; k++) {
        int body = -1;

        for (i = 0; i < g_cars_plc[0]; i++) {
            if (plc_at(g_cars_plc, i)->type == 10 * k) {
                body = i;
                break;
            }
        }
        if (body >= 0) {
            const struct plc_entry *b = plc_at(g_cars_plc, body);
            s32 *m = g_cars_msh + b->mesh;
            s32 top = -1000;

            found_any = 1;
            g_car_meshes[k] = m;
            /* The body's top goes to y 0 (mesh y points down). */
            for (i = 0; i < m[0]; i++) {
                if (top < m[3 + 3 * i]) {
                    top = m[3 + 3 * i];
                }
            }
            for (i = 0; i < m[0]; i++) {
                m[3 + 3 * i] -= top;
            }
            if (g_cheats[0] == 1) {
                for (i = 0; i < m[0]; i++) {
                    m[3 + 3 * i] *= 3;
                }
            }
            for (w = 0; w < 4; w++) {
                int found = 0;

                for (i = 0; i < g_cars_plc[0]; i++) {
                    const struct plc_entry *e = plc_at(g_cars_plc, i);
                    s32 *wm;
                    s32 most = -1000;
                    int v;

                    if (e->type - w - 10 * k != 1) {
                        continue;
                    }
                    found = 1;
                    wm = g_cars_msh + e->mesh;
                    g_wheel_meshes[4 * k + w] = wm;
                    if (mesh_dwords(wm) > WHEEL_DWORDS) {
                        log_printf("TOO LITTLE WHEEL MSH MEMORY!!\n");
                        exit(1);
                    }
                    g_wheel_dx[4 * k + w] = e->x - b->x;
                    g_wheel_dz[4 * k + w] = e->z - b->z;
                    for (v = 0; v < wm[0]; v++) {
                        if (wm[4 + 3 * v] > most) {
                            most = wm[4 + 3 * v];
                        }
                    }
                    g_wheel_info[4 * k + w].verts = wm[0];
                    g_wheel_info[4 * k + w].top = abs(most);
                    g_wheel_info[4 * k + w].dz = abs(g_wheel_dz[4 * k + w]);
                }
                if (!found) {
                    log_printf("DIDN'T FOUND WHEEL %d ON CAR %d!!\n", w, g_drivers[k].car);
                    exit(1);
                }
            }
            rear[k] = abs(g_wheel_dx[4 * k + 2]);
            front[k] = abs(g_wheel_dx[4 * k]);
        }
        /* Player cars have their own curves; the computer's share one
         * set. The original asks driver k, not a driver of car k. */
        for (i = 0; i < 3; i++) {
            curve_build(g_drivers[k].control == 1 ? g_ai_curves + 8 * i
                                                  : g_car_curves + 24 * k + 8 * i,
                        g_car_curves_built[k][i]);
        }
    }
    if (!found_any) {
        log_printf("DIDN'T FOUND CAR %d!!\n", g_drivers[0].car);
        exit(1);
    }
}

/* Each driver's own copy of their car's wheels, moved to the wheels'
 * places. */
static void copy_wheels(void)
{
    int d, w, i, v;

    for (d = 0; d < g_driver_count; d++) {
        s32 car = g_drivers[d].car;

        for (w = 0; w < 4; w++) {
            for (i = 0; i < g_cars_plc[0]; i++) {
                const struct plc_entry *e = plc_at(g_cars_plc, i);
                const s32 *src;
                s32 *dst = g_driver_wheels[d][w];

                if (10 * car - e->type + w != -1) {
                    continue;
                }
                src = g_cars_msh + e->mesh;
                memcpy(dst, src, (size_t)mesh_dwords(src) * sizeof(s32));
                for (v = 0; v < src[0]; v++) {
                    dst[2 + 3 * v] += g_wheel_dx[4 * car + w];
                    dst[4 + 3 * v] += g_wheel_dz[4 * car + w];
                }
            }
        }
    }
}

/* The grid: from the start piece, back 200 and 180 a row, in two
 * columns 60 either side; alone, 200 back. */
static void start_place(int i, double *x, double *y, double *z, double *heading)
{
    const struct track_link *l = &g_track_links[0];
    const struct plc_entry *p = plc_at(g_level_plc, l->plc);
    double a = l->heading - 1.5707963267949;

    *x = p->x + 0x6400;
    *z = p->z + 0x6400;
    *heading = angle_wrap(a);
    if (g_driver_count == 1) {
        *y = 40 - p->y;
        *x += cos(a) * -200.0;
        *z += sin(a) * -200.0;
    } else {
        s32 g = g_race_grid[i], row = g / 2;
        double back = -(row * 180) - 200, side = (2 * row - g) == 0 ? 60 : -60;

        *y = 30 - p->y;
        *x += back * cos(a) - side * sin(a);
        *z += back * sin(a) + side * cos(a);
    }
}

static void init_car(int i, const s32 rear[CAR_MODELS], const s32 front[CAR_MODELS])
{
    struct car *c = &g_cars[i];
    struct car_phys *ph = &g_car_phys[i];
    struct race_driver *d = &g_drivers[i];
    struct car_hull *hull = &g_car_hulls[i];
    const s32 *body = g_car_meshes[d->car];
    double x, y, z, h;
    s32 max_x = -1000, max_z = -1000, min_x = 1000, min_z = 1000;
    int k;

    start_place(i, &x, &y, &z, &h);
    c->f568 = 0;
    c->x = x;
    c->y = y;
    c->z = z;
    c->f108 = 0;
    c->heading = h;
    c->f100 = 0;
    c->f110 = 0;
    /* The axles; the original takes their lengths for driver i's car
     * model number, not for the car. */
    c->rear_x = c->x - cos(h) * rear[i];
    c->rear_z = c->z - sin(h) * rear[i];
    c->front_x = c->x + cos(h) * front[i];
    c->front_z = c->z + sin(h) * front[i];
    c->f058 = c->f060 = 0;
    c->f078 = c->f080 = 0;
    c->f0a8 = 10.0;
    c->f0c8 = 0.97;
    c->f0d0 = 0.97;
    c->f0d8 = 35.0;
    c->f0e0 = 20.0;
    c->f0e8 = 50.0;
    c->f0f0 = 80.0;
    c->surface[0] = c->surface[1] = c->surface[2] = c->surface[3] = 0;
    c->f120 = y;
    c->f128 = y;
    c->f148 = 0.4;
    c->f270 = 0;
    c->f130 = 0.01;
    c->steer = c->f348 = c->f34c = 0;
    c->gear = 1;
    c->f290 = 0;
    c->f280 = c->gear * 5 - 5;
    c->f350 = 0;
    c->f364 = c->f568;
    c->f368 = c->f364;
    c->f36c = c->f370 = 0;
    for (k = 0; k < 8; k++) {
        c->f3dc[k].v = 0;
    }
    c->f634 = 10.0f;
    c->f638 = 4000.0f;
    c->f51c = c->f520 = c->f528 = 0;
    c->grid = g_race_grid[i];
    c->f3a0 = 0;
    c->f534 = 100.0;
    c->f53c = c->f540 = c->f544 = c->f548 = c->f54c = 0;
    c->f554 = c->f558 = c->f564 = 0;
    c->f378 = (s32)((c->f568 + 4) / (double)g_track_link_count * 4.0);
    c->f340 = 0;
    c->f4834 = c->f483c = 0;
    c->wrong_way = 0;
    c->f584 = (c->f108 * 57.295779513082195 + c->f0b8) * 10.0;
    c->f58c = (c->f110 * 57.295779513082195 + c->f0c0) * 10.0;
    c->f594 = 1.0;
    c->f59c = 0;
    c->rear_axle = rear[d->car];
    c->front_axle = front[d->car];
    c->f5c4 = c->f604 = c->f550 = 0;
    c->f60c = c->clock = c->f61c = c->f624 = c->f62c = 0;

    ph->mass = g_car_4923b8[d->car];
    ph->inertia = 8000.0;
    ph->f010 = 0.6;
    ph->rear_len = fabs(g_wheel_dx[4 * d->car] * 0.04596) * 1.5;
    ph->front_len = fabs(g_wheel_dx[4 * d->car + 2] * 0.04596) * 2.0;
    ph->heading = c->heading;
    ph->yaw_rate = 0;
    for (k = 0; k < 3; k++) {
        ph->f038[k] = c->x * 0.04596;
        ph->f050[k] = c->z * 0.04596;
    }
    ph->x = c->x * 0.04596;
    ph->z = c->z * 0.04596;
    ph->vx = ph->vz = ph->f088 = ph->f090 = ph->speed = 0;
    ph->roll = ph->pitch = 0;
    ph->traction = 4.2;
    ph->grip = 4.2;
    ph->brake = 0;
    ph->steer = 0;
    ph->drive = 0;

    if (d->car == 7) {
        static const s32 car7[0x23] = {
            4, 4, 15, -10, 10, 15, -25, 10, 15, -40, 10, 15, -55, 10, 11, 0, 1, 0xf4,
            11, 1, 2, 0xf4, 11, 2, 3, 0xf4, 7, 3, 0, 0x7500, 0x1f00, 0x9400, 0, 0, 0
        };

        memcpy(g_car7[i], car7, sizeof(car7));
        g_car7[i][0x21] = r_552d80 * 50;
        g_car7[i][0x22] = r_552f5c * 50;
    }
    if (i == 0) {
        r_5530bc = (s32)c->x;
        r_5530c0 = (s32)c->y;
        r_5530c4 = (s32)c->z;
    }

    for (k = 0; k < body[0]; k++) {
        const s32 *v = body + 2 + 3 * k;

        max_x = v[0] > max_x ? v[0] : max_x;
        max_z = v[2] > max_z ? v[2] : max_z;
        min_x = v[0] < min_x ? v[0] : min_x;
        min_z = v[2] < min_z ? v[2] : min_z;
    }
    c->corner_x[0] = c->corner_x[1] = min_x * 1.1;
    c->corner_x[2] = c->corner_x[3] = max_x * 1.1;
    c->corner_z[0] = c->corner_z[3] = min_z * 1.1;
    c->corner_z[1] = c->corner_z[2] = max_z * 1.1;
    ph->corner_x[0] = ph->corner_x[1] = min_x * 0.050556000000000004;
    ph->corner_x[2] = ph->corner_x[3] = max_x * 0.050556000000000004;
    ph->corner_z[0] = ph->corner_z[3] = min_z * 0.050556000000000004;
    ph->corner_z[1] = ph->corner_z[2] = max_z * 0.050556000000000004;
    ph->f130 = 4.0;
    c->max_x = max_x;
    c->min_x = min_x;
    c->max_z = max_z;
    c->min_z = min_z;

    /* The hull the collisions use: the body's box at y 0, two faces. */
    {
        const s32 h0[0x19] = {
            4, 2, max_x, 0, max_z, min_x, 0, max_z, max_x, 0, min_z, min_x, 0, min_z,
            20, 0, 1, 3, 40, 20, 0, 3, 2, 40, 0
        };

        memcpy(hull->v, h0, sizeof(h0));
    }
}

/* 0x41d190 */
void race_load_cars(void)
{
    s32 rear[CAR_MODELS] = { 0 }, front[CAR_MODELS] = { 0 };
    int i;

    g_cars = mem_calloc(g_race_pool, (u32)g_driver_count * sizeof(*g_cars));
    g_race_bytes += g_driver_count * (s32)sizeof(*g_cars);
    if (g_cars == NULL) {
        mem_out_of_memory();
    }
    g_wheel_info = mem_calloc(g_race_pool, CAR_MODELS * 4 * sizeof(*g_wheel_info));
    g_race_bytes += g_driver_count * 0x30;
    if (g_wheel_info == NULL) {
        mem_out_of_memory();
    }
    g_car_phys = mem_calloc(g_race_pool, (u32)g_driver_count * sizeof(*g_car_phys));
    g_race_bytes += g_driver_count * (s32)sizeof(*g_car_phys);
    if (g_car_phys == NULL) {
        mem_out_of_memory();
    }
    pick_ai_cars();
    load_models(rear, front);
    copy_wheels();
    for (i = 0; i < g_driver_count; i++) {
        init_car(i, rear, front);
    }
}
