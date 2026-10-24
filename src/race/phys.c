/*
 * race/phys.c - the race's physics step (0x422680) and the parts of it
 * that move the cars: falling and landing, the body's sway, the race
 * positions and the ghost recording.
 *
 * The step runs in halves of a tick (1/72 s): each half moves every car
 * on the ground, collides the cars with each other and with the walls,
 * and works out where each is going. The drivers (the AI and the stored
 * controls) run every other half.
 */

#include <math.h>

#include "../phys.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/ai.h"
#include "race/contact.h"
#include "race/control.h"
#include "race/frame.h"
#include "race/init.h"
#include "race/phys.h"
#include "scene.h"
#include "srf.h"
#include "stubs.h"

extern double r_54f948, r_54f980;
extern s32 *g_ghost_buffer;
extern s32 r_5531b8, r_563d44, r_552f54, r_563c60, r_5530e0, r_552f10, r_553068;

s32 r_6035e0;                   /* 1 the ghost leads, 2 the player */

static const double TWO_PI = 6.2831853071796;

/* 0x446390: a into 0 .. m. */
static double angle_mod(double a, double m)
{
    if (a < 0.0) {
        do {
            a += m;
        } while (a < 0.0);
        return a;
    }
    while (a >= m) {
        a -= m;
    }
    return a;
}

static s32 clamp_volume(s32 v)
{
    return v > 0x10000 ? 0x10000 : v;
}

/* Whether the network moves car i here: another player's car, or the
 * host's computer cars on a client. */
static int is_remote(int i)
{
    s32 control = g_drivers[i].control;

    return (control == 2 && i != 0) || (control == 1 && r_552f10 == 4);
}

static int is_out(const struct car *c)
{
    return c->f354 || c->f358 || c->f35c || c->f350;
}

/* 0x426b40: is_remote's cars follow the packets. Not ported yet. */
static void race_contact_remote(int car, int code)
{
    (void)car; (void)code;
    STUB(0x426b40, "race_contact_remote");
}

/* 0x427d70: the middle of the car's move, between its axles'. */
void race_car_center(int i)
{
    struct car *c = &g_cars[i];

    c->center_dx = (c->f058 + c->f078) * 0.5;
    c->center_dz = (c->f080 + c->f060) * 0.5;
}

/* 0x423ea0: a car from the network touching down. */
static void race_remote_land(int i)
{
    struct car *c = &g_cars[i];

    if (c->f120 + 5.0 > c->y && c->f270 == 1) {
        c->f270 = 0;
        c->landing = 1;
    }
}

/* 0x423f00: without a packet this frame, a car from the network carries
 * on at its speed. */
static void race_remote_drift(int i)
{
    struct car_phys *p = &g_car_phys[i];

    if (g_cars[i].net_fresh == 0) {
        p->x = p->vx * 0.013888888888888888 + p->x;
        p->z = p->vz * 0.013888888888888888 + p->z;
    }
}

/* The explosion and splash effects the fall starts (0x639330). */
static void fall_effect(int i, int kind, int set_last)
{
    const struct car *c = &g_cars[i];

    g_effect.type = kind;
    g_effect.x = (s32)(c->rear_x * 1024.0);
    g_effect.y = (s32)(c->y * 1024.0);
    g_effect.z = (s32)(c->rear_z * 1024.0);
    g_effect.f10 = i;
    g_effect.f14 = 0;
    g_effect.f18 = 80;
    if (set_last) {
        g_effect.f1c = 1;
    }
    race_effect(&g_effect);
}

/* 0x423aa0
 * Gravity and the ground. A car falls by vy, 0.2 more each step, and
 * stops 5 above the ground. Landing from the air bounces it (a fifth
 * back up from a hard landing); driving down a slope it keeps the
 * slope's fall, unless that is steep for the speed. Landing on a
 * deadly surface or too hard blows it up; on water it sinks. When the
 * player's car has finished on autopilot the others are lifted out of
 * the way. */
static void race_car_fall(int i)
{
    struct car *c = &g_cars[i];
    double slope = c->f128 - c->f120, floor;
    s32 type;

    if (fabs(c->f118) < 50.0 && fabs(slope) > 4.0) {
        slope = 0.0;
    }
    if (fabs(c->f118 * 0.05) < slope) {
        slope = 0.0;
    }
    c->vy += 0.2;
    c->y -= c->vy;
    floor = c->f120 + 5.0;
    if (floor > c->y) {
        c->y = floor;
        if (c->f270 == 0) {
            c->vy = slope;
        } else if (c->f270 == 1) {
            c->vy *= c->vy > 8.0 ? -0.2 : 0.5;
            if (c->vy < -15.0) {
                c->vy = -15.0;
            }
            c->f270 = 0;
            c->landing = 1;
            type = c->srf_cache[0];
            if (type == 100
                || (((type >= 110 && type <= 130) || (type >= 20 && type < 30)) && c->deck == 1)
                || slope < -35.0) {
                c->f354 = 1;
                fall_effect(i, 3, 1);
                c->net_event = -1;
            }
            type = c->srf_cache[0];
            if (type == 101
                || (((type >= 130 && type <= 150) || (type >= 30 && type < 40)) && c->deck == 1)) {
                c->f358 = 1;
                fall_effect(i, 7, 0);
                c->net_event = -2;
            }
            race_sound_3d(0, 2, 3, 0, clamp_volume((s32)(fabs(c->vy * 64.0) + 4096.0)), 0x55f0, 0,
                          (float)(s32)c->x, (float)(s32)c->z);
        }
    }
    if (g_cars[0].f528 == 1 && g_cars[0].f604 == 1) {
        int k;

        for (k = 1; k < g_driver_count; k++) {
            g_cars[k].y = 1800.0;
        }
    }
}

/* 0x423f70
 * The body leans against the car's acceleration, turned into the car's
 * frame, springing back by the damping (0.97) and held to the limits
 * (35 and 20). Car 7 also moves the parts the table at 0x527f80
 * draws. */
static void race_car_sway(int i)
{
    struct car *c = &g_cars[i];
    double ax = c->dx - c->prev_dx, az = c->dz - c->prev_dz, t;

    if (ax > 0.5) {
        ax = 0.5;
    }
    if (ax < -0.5) {
        ax = -0.5;
    }
    if (az > 0.5) {
        az = 0.5;
    }
    if (az < -0.5) {
        az = -0.5;
    }
    t = TWO_PI - c->heading;
    c->f0b8 = (cos(t) * az + sin(t) * ax) * -3.0 + c->f0b8;
    t = TWO_PI - c->heading;
    c->f0c0 = (sin(t) * az - cos(t) * ax) * 3.0 + c->f0c0;
    c->f0c0 = c->f0d0 * c->f0c0;
    c->f0b8 = c->f0c8 * c->f0b8;
    if (c->f0e0 < c->f0c0) {
        c->f0c0 = c->f0e0;
    }
    if (-c->f0e0 > c->f0c0) {
        c->f0c0 = -c->f0e0;
    }
    if (c->f0d8 < c->f0b8) {
        c->f0b8 = c->f0d8;
    }
    if (-c->f0d8 > c->f0b8) {
        c->f0b8 = -c->f0d8;
    }
    if (g_drivers[i].car == 7) {
        s32 *t7 = g_car7[i];

        t7[5] = (s32)(c->f0c0 * 0.15 + 15.0);
        t7[7] = (s32)(c->f0b8 * 0.15 + 10.0);
        t7[8] = (s32)(c->f0c0 * 0.4 + 15.0);
        t7[10] = (s32)(c->f0b8 * 0.4 + 10.0);
        t7[11] = (s32)(c->f0c0 + 15.0);
        t7[13] = (s32)(c->f0b8 + 10.0);
        t7[33] = r_552d80 * 50;
        t7[34] = r_552f5c * 50;
    }
}

/* A car's progress: laps, then the segment, then the frames in it, 30
 * to a segment. Near the line before a quarter of the lap the segment
 * does not count yet. Shared by the positions and the ghost. */
static s32 progress(const struct car *c, int frames)
{
    s32 laps = (g_track_link_count + 10) * c->laps;
    s32 seg = c->f364 < 0 ? -c->f364 : c->f364;
    s32 p = laps + seg;

    if (g_track_link_count / 4 <= seg && c->f378 == 0) {
        p = laps;
    }
    return frames ? p * 30 + c->seg_frames : p;
}

/* 0x440c90
 * The places: cars by progress, finished cars by their finishing place
 * (in an elimination race the place knocked out counts against them).
 * Cars that finished in the same place are ordered by time. Before the
 * start the grid holds, and racing a ghost the player is first or
 * second to it. */
void race_positions(void)
{
    s32 score[RACE_DRIVERS], order[RACE_DRIVERS];
    double times[RACE_DRIVERS];
    int i, j, k, n = g_driver_count;

    r_5530e0 = 0;
    for (i = 0; i < n; i++) {
        const struct car *c = &g_cars[i];

        if (c->f528 == 0) {
            score[i] = progress(c, 1);
        } else if (g_race_mode == 3) {
            score[i] = ((g_track_link_count + 10) * c->laps - c->f3a0 * 10) * 30 + c->seg_frames;
        } else {
            score[i] = ((g_track_link_count + 10) * c->laps + (n - c->f3a0) * 10) * 30 + c->seg_frames;
            if (g_race_clock < 250.0) {
                score[i] -= 11;
            }
        }
        order[i] = i;
    }
    if (n > 1) {
        for (j = n - 1; j >= 0; j--) {
            for (k = 1; k <= j; k++) {
                if (score[k - 1] < score[k]) {
                    s32 t = score[k];

                    score[k] = score[k - 1];
                    score[k - 1] = t;
                    t = order[k];
                    order[k] = order[k - 1];
                    order[k - 1] = t;
                }
            }
        }
    }
    for (k = 0; k < n; k++) {
        g_cars[order[k]].grid = k + 1;
    }
    for (i = 0; i < n; i++) {
        if (g_cars[i].f528 != 1) {
            continue;
        }
        for (j = i + 1; j < n; j++) {
            if (g_cars[j].f528 != 1 || g_cars[i].f3a0 != g_cars[j].f3a0) {
                continue;
            }
            for (k = 0; k < n; k++) {
                order[k] = k;
                times[k] = g_cars[k].f3a4;
            }
            for (int m = n - 1; m >= 0; m--) {
                for (k = 1; k <= m; k++) {
                    if (times[k] < times[k - 1]) {
                        double t = times[k];
                        s32 o = order[k];

                        times[k] = times[k - 1];
                        times[k - 1] = t;
                        order[k] = order[k - 1];
                        order[k - 1] = o;
                    }
                }
            }
            for (k = 0; k < n; k++) {
                g_cars[order[k]].f3a0 = k + 1;
            }
        }
    }
    if (g_race_clock < 250.0 && g_cars[0].f528 == 0) {
        for (k = 0; k < n; k++) {
            g_cars[k].grid = g_race_grid[k] + 1;
        }
    }
    if (r_553068 == 1 && g_cars[0].f528 == 0) {
        const s16 *ghost = (const s16 *)g_ghost_buffer + r_552f54 * 7;

        if (ghost[6] > score[0]) {
            g_cars[0].grid = 2;
            r_6035e0 = 1;
        } else {
            g_cars[0].grid = 1;
            r_6035e0 = 2;
        }
    }
}

/* 0x445c10
 * Records the player's car for the ghost: seven s16 a step (x and z
 * less 25600, y, pitch, heading and roll in 1/1024 radians, and the
 * progress), 21600 steps; full, recording stops. */
static void race_ghost_record(void)
{
    const struct car *c = &g_cars[0];
    s16 *f;

    if (r_563c60 == 1 || !(0.0 <= g_race_clock)) {
        return;
    }
    f = (s16 *)g_ghost_buffer + r_552f54 * 7;
    f[0] = (s16)(s32)(c->x - 25600.0);
    f[1] = (s16)(s32)(c->f354 ? c->y + 5000.0 : c->y);
    f[2] = (s16)(s32)(c->z - 25600.0);
    f[3] = (s16)(s32)(angle_wrap(c->f108) * 1024.0);
    f[4] = (s16)(s32)(angle_wrap(c->heading) * 1024.0);
    f[5] = (s16)(s32)(angle_wrap(c->f110) * 1024.0);
    f[6] = (s16)progress(c, 0);
    if (++r_552f54 >= 21600) {
        r_552f54 = 0;
        r_5530b8 = 1;
    }
}

/* 0x4242b0: two cars hit hard: a crunch and six bits flying from where
 * they touched. */
static void race_crash_fx(int a, int b)
{
    const struct car_phys *pa = &g_car_phys[a], *pb = &g_car_phys[b];
    const struct car *ca = &g_cars[a], *cb = &g_cars[b];
    s32 vol = (s32)(fabs(pb->impact + pa->impact) * 0.5 * 30.0);
    int k;

    race_sound_3d(0, 2, 0, 0, clamp_volume(vol), 0x55f0, 0, (float)ca->x, (float)ca->z);
    for (k = 0; k < 6; k++) {
        g_effect.type = 5;
        g_effect.x = (s32)(pa->contact_x * 22282.24);
        g_effect.y = (s32)((cb->y + ca->y) * 0.5 * 1024.0);
        g_effect.z = (s32)(pa->contact_z * 22282.24);
        g_effect.f10 = (s32)((race_rand() * 6.0 + (cb->center_dx + ca->center_dx) * 0.5 - 3.0) * 1024.0);
        g_effect.f14 = (s32)((race_rand() * 3.0 + 3.0) * 1024.0);
        g_effect.f18 = (s32)((race_rand() * 6.0 + (cb->center_dz + ca->center_dz) * 0.5 - 3.0) * 1024.0);
        g_effect.f1c = 0x400;
        g_effect.f20 = -0x266;
        g_effect.f24 = 0;
        g_effect.f28 = 0;
        g_effect.f2c = 0;
        g_effect.f30 = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
        g_effect.f34 = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
        g_effect.f38.i = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
        g_effect.f3c = 0;
        g_effect.f40 = 1;
        g_effect.f44 = 0;
        g_effect.f48 = 0;
        g_effect.f4c = 15;
        race_effect(&g_effect);
    }
}

/* Part A of a half step: car i on the ground and in the air. */
static void step_car(int i)
{
    struct car *c = &g_cars[i];
    struct car_phys *p = &g_car_phys[i];
    int remote = is_remote(i);

    c->probe_scale = c->f118 * 0.004545454545454545 + 1.0;
    if (is_out(c)) {
        return;
    }
    if (remote) {
        race_contact_remote(i, c->net_event);
    } else {
        race_contact(i);
    }
    if (c->landing > 0) {
        if (c->landing == 1) {
            race_sound_3d(0, 2, 9, 0, clamp_volume((s32)(fabs(c->vy * 8192.0) + 4096.0)), 0x55f0, 0,
                          (float)(s32)c->x, (float)(s32)c->z);
        }
        if (++c->landing > 4) {
            c->landing = 0;
        }
    }
    p->roll = c->f110;
    p->pitch = c->f108;
    if (remote) {
        if (c->f528 == 0) {
            race_remote_drift(i);
            race_remote_land(i);
        }
    } else {
        phys_step(p, c->f270, c->f148 > 0.4 ? 1.0 : 4.0);
        race_car_fall(i);
        if (r_552f10 != 2) {
            race_car_sway(i);
        }
    }
    if (c->f120 + 30.0 < c->y) {
        c->f270 = 1;
    }
    phys_corners(p, c->probe_scale);
}

/* Part C: cars i and j close together. One falling onto the other
 * squashes it. */
static void collide_pair(int i, int j)
{
    struct car *a = &g_cars[i], *b = &g_cars[j];
    double adz = fabs(a->z - b->z);

    if (!(fabs(a->x - b->x) < 200.0) || !(adz < 200.0)) {
        return;
    }
    if (a->f354 || a->f358 || a->f35c || b->f354 || b->f358 || b->f35c
        || a->f350 || b->f350 || a->f558 || b->f558) {
        return;
    }
    if (!(fabs(a->y - b->y) < 70.0)) {
        return;
    }
    if (fabs(a->x - b->x) < 25.0 && adz < 25.0) {
        if (a->vy < b->vy) {
            a->f558 = 1;
            a->f55c = 1;
            if (g_drivers[i].control != 2 || i == 0) {
                a->f560 = 1;
            }
            race_squash(i);
        } else {
            b->f558 = 1;
            b->f55c = 1;
            if (g_drivers[j].control != 2 || j == 0) {
                b->f560 = 1;
            }
            race_squash(j);
        }
    }
    if (phys_collide(&g_car_phys[i], &g_car_phys[j]) != 1) {
        return;
    }
    if ((g_car_phys[i].impact + g_car_phys[j].impact) * 0.5 > 15.0) {
        race_crash_fx(i, j);
    }
    race_sound_3d(0, 2, 3, 0,
                  clamp_volume((s32)fabs((g_car_phys[i].impact + g_car_phys[j].impact) * 400.0)),
                  0x55f0, 0, (float)(s32)a->x, (float)(s32)a->z);
}

/* Part D: a car that touched a wall. The walls near it come from the
 * polygons the scene sees from above the car, gathered again once it
 * has moved 150 from where they were. */
static void collide_walls(int i)
{
    struct car *c = &g_cars[i];
    struct car_phys *p = &g_car_phys[i];
    int count = 0;

    if (!is_out(c) && !is_remote(i)) {
        const struct race_cam *cam = &g_race_cams[i];
        s32 plc = c->f364 >= 0 ? g_track_links[c->f364].plc : g_track_links[-c->f364].alt_plc;

        g_scene_group = r_54f980 < 120.0 ? g_plc_extra[plc].f0 : 0;
        g_scene->stage_lookat = 0;
        g_scene->x = c->x;
        g_scene->y = c->y + 500.0;
        g_scene->z = c->z;
        g_scene->pitch = 850.0;
        g_scene->yaw = 0.0;
        g_scene->roll = 0.0;
        g_scene->target_roll = 0;
        g_scene->focal_x = cam->f58;
        g_scene->focal_y = cam->f5c;
        g_scene->near_ofs = (s32)cam->f60;
        scene_gather_view();
        if (!(fabs(c->x - c->f4834) <= 150.0) || !(fabs(c->z - c->f483c) <= 150.0)) {
            c->wall_count = race_wall_edges(c->wall_segs, (s32)c->x, (s32)c->y, (s32)c->z, c->deck);
            c->f4834 = c->x;
            c->f483c = c->z;
        }
        count = c->wall_count;
    }
    if (c->f35c == 0) {
        phys_walls(p, c->wall_segs, count);
    }
    if (p->impact > 5.0) {
        race_sound_3d(0, 2, 3, 0, clamp_volume((s32)((p->impact - 5.0) * 2500.0)), 0x55f0, 0,
                      (float)(s32)c->x, (float)(s32)c->z);
    }
    p->impact = 0.0;
    c->wall_contact = 0;
}

/* Part E: from the body back to the car: heading, position, the axles
 * and how each moves, the direction of travel and the speed. */
static void update_motion(int i)
{
    struct car *c = &g_cars[i];
    const struct car_phys *p = &g_car_phys[i];
    double front, cs, sn;
    int slips = r_552f10 == 2 || (r_552f10 == 3 && g_drivers[i].control == 1) || i == 0;

    c->heading = angle_wrap(p->heading);
    c->prev_dx = c->dx;
    c->prev_dz = c->dz;
    c->dx = p->x * 21.76 - c->x;
    c->dz = p->z * 21.76 - c->z;
    c->x = p->x * 21.76;
    c->z = p->z * 21.76;
    front = (double)c->front_axle;
    cs = cos(c->heading);
    sn = sin(c->heading);
    c->f058 = c->x - cs * (double)c->rear_axle - c->rear_x;
    c->f060 = c->z - sn * (double)c->rear_axle - c->rear_z;
    c->f078 = cs * front + c->x - c->front_x;
    c->f080 = sn * front + c->z - c->front_z;
    c->rear_x = c->x - cs * (double)c->rear_axle;
    c->rear_z = c->z - sn * (double)c->rear_axle;
    c->front_x = cs * front + c->x;
    c->front_z = sn * front + c->z;
    if (c->skid > 0) {
        c->skid--;
    }
    c->f100 = c->dz == 0.0 && c->dx == 0.0 ? 0.0 : atan2(c->dz, c->dx);
    c->f118 = fabs(p->speed * 3.6);
    c->rear_dir = c->f060 == 0.0 && c->f058 == 0.0 ? 0.0 : atan2(c->f060, c->f058);
    if (slips) {
        c->slip = angle_mod(c->rear_dir - c->heading, TWO_PI);
    }
    c->front_dir = c->f080 == 0.0 && c->f078 == 0.0 ? 0.0 : atan2(c->f080, c->f078);
    if (slips) {
        c->front_slip = angle_mod(c->front_dir - (p->steer + c->heading), TWO_PI);
    }
}

/* Part F: the drivers, every other half step. The computer plans for
 * one car a step, in turn; the others replay their last controls. */
static void drive(int i)
{
    const struct car *c = &g_cars[i];

    if (g_drivers[i].control != 1 && c->f604 != 1) {
        return;
    }
    r_5285c0 = i;
    if (r_552f10 != 2 && r_552f10 != 3) {
        return;
    }
    if ((g_drivers[i].f0c == 1 && c->gear != 0) || c->f604 == 1) {
        race_gearbox(i);
    }
    if (r_563d44 == i && !(0.0 <= r_552e48 && r_552e48 <= 170.0)) {
        race_ai(i);
    } else {
        race_control(RACE_CONTROL_REPEAT, 0, 0, 0);
    }
}

/* 0x422680 */
void race_physics(double dt)
{
    int i, j, n;

    r_54f948 += dt;
    if (r_54f948 <= 0.5) {
        return;
    }
    do {
        n = g_driver_count;
        for (i = 0; i < n; i++) {
            step_car(i);
        }
        race_positions();
        if (g_race_mode == 3) {
            race_knockout();
        }
        for (i = 0; i < g_driver_count; i++) {
            g_cars[i].net_fresh = 0;
        }
        for (i = 0; i < g_driver_count - 1; i++) {
            for (j = i + 1; j < g_driver_count; j++) {
                collide_pair(i, j);
            }
        }
        for (i = 0; i < g_driver_count; i++) {
            if (g_cars[i].wall_contact == 1) {
                collide_walls(i);
            }
        }
        for (i = 0; i < g_driver_count; i++) {
            if (is_out(&g_cars[i])) {
                continue;
            }
            update_motion(i);
            if (r_5531b8 == 1) {
                drive(i);
            }
        }
        if (r_5531b8 == 0 && ++r_563d44 >= g_driver_count) {
            r_563d44 = 0;
        }
        if (g_race_mode == 2 && r_552e48 == -1.0 && r_5530b8 == 0) {
            race_ghost_record();
        }
        r_5531b8 ^= 1;
        r_54f948 -= 0.5;
    } while (r_54f948 > 0.5);
}
