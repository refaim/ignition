/*
 * race/respawn.c - putting a car back on the road (0x435910) and the
 * wrong way timer (0x43edf0).
 */

#include <math.h>
#include <string.h>

#include "lisa/log.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/init.h"
#include "race/respawn.h"
#include "scene.h"
#include "srf.h"

static const double TWO_PI = 6.2831853071796;
static const double HALF_PI = 1.5707963267949;

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

/* The road two pieces ahead of `seg` (on the fork's second branch for
 * a negative one): its placement and the heading across it. */
static s32 road_ahead(s32 seg, double *heading)
{
    s32 aseg = seg < 0 ? -seg : seg, plc;
    double h;

    if (aseg + 2 >= g_track_link_count) {
        plc = g_track_links[0].plc;
        h = g_track_links[0].heading - HALF_PI;
    } else if (seg >= 0) {
        plc = g_track_links[seg + 2].plc;
        h = g_track_links[seg + 2].heading - HALF_PI;
    } else {
        int k = aseg + 2;

        plc = g_track_links[k].alt_plc;
        h = g_track_links[k].alt_heading - HALF_PI;
        if (h < 0.0) {
            h += TWO_PI;
        }
        if (plc == 10000) {
            /* The original indexes the main branch with the negative
             * segment here; below piece 0 it reads before the table. */
            int m = seg + 2;

            if (m < 0) {
                log_printf("race_respawn: segment %d before the track\n", (int)seg);
                m = 0;
            }
            plc = g_track_links[m].plc;
            h = g_track_links[m].heading - HALF_PI;
            if (h < 0.0) {
                h += TWO_PI;
            }
        }
        if (plc == -2) {
            int j = 0;

            do {
                j++;
            } while (k + j < g_track_link_count && g_track_links[k + j].alt_plc == -2);
            plc = g_track_links[aseg + j + 2].plc;
            h = g_track_links[aseg + j + 2].heading - HALF_PI;
        } else {
            *heading = h;
            return plc;
        }
    }
    if (h < 0.0) {
        h += TWO_PI;
    }
    *heading = h;
    return plc;
}

/* 0x435910
 * Puts the car r_5285c0 back on the road two pieces ahead of where it
 * was: at the middle of the piece's drivable faces, 250 up, facing
 * along the road, stopped and in first gear. The ground under it comes
 * from the scene seen from above (20 to the side when there is none
 * under the middle). A squashed car gets its body and wheels back. */
void race_respawn(void)
{
    int i = r_5285c0, model = g_drivers[i].car, w, v, f, nf;
    struct car *c = &g_cars[i];
    struct car_phys *p = &g_car_phys[i];
    const struct race_cam *cam = &g_race_cams[i];
    const struct plc_entry *e;
    const s32 *msh, *face;
    const struct srf_hit *hit;
    s32 plc, px, py, pz, sx = 0, sy = 0, sz = 0, n = 0, mx, my, mz;
    double h, ground;

    plc = road_ahead(c->f364, &h);
    e = plc_at(g_level_plc, plc);
    px = e->x + 25600;
    py = e->y;
    pz = e->z + 25600;
    msh = g_level_msh + e->mesh;
    nf = msh[1];
    face = msh + 2 + 3 * msh[0];
    for (f = 0; f < nf; f++, face += 11) {
        const s32 *a = msh + 2 + 3 * face[1], *b = msh + 2 + 3 * face[2], *d = msh + 2 + 3 * face[3];

        if (((u32)face[0] & 0xffff0000u) >= 0x280000u) {
            continue;
        }
        sx += a[0] + px + b[0] + px + d[0] + px;
        sy += py - a[1] + py - b[1] + py - d[1];
        sz += a[2] + pz + b[2] + pz + d[2] + pz;
        n += 3;
    }
    if (n == 0) {
        /* The original divides by zero here. */
        log_printf("race_respawn: no road faces in placement %d\n", (int)plc);
        return;
    }
    mx = sx / n;
    my = sy / n + 250;
    mz = sz / n;

    g_scene_group = g_plc_extra[plc].f0;
    g_scene->stage_lookat = 0;
    g_scene->x = mx;
    g_scene->y = my + 500;
    g_scene->z = mz;
    g_scene->pitch = 850.0;
    g_scene->yaw = 0.0;
    g_scene->roll = 0.0;
    g_scene->target_roll = 0;
    g_scene->focal_x = cam->f58;
    g_scene->focal_y = cam->f5c;
    g_scene->near_ofs = (s32)cam->f60;
    scene_gather_view();
    hit = race_obj_query(mx, my, mz, -1, 50);
    if (hit->type == -1) {
        mx -= 20;
        hit = race_obj_query(mx, my, mz, -1, 50);
        if (hit->type == -1) {
            log_printf("N");
        }
    }
    for (w = 0; w < 4; w++) {
        memcpy(&c->srf_cache[16 * w], hit, sizeof(*hit));
    }
    {
        double ny = hit->normal[1];
        s32 dx = mx - hit->v[0][0], dz = mz - hit->v[0][2];
        s32 a = hit->v[1][2] - hit->v[0][2], b = hit->v[1][0] - hit->v[0][0];
        s32 cy = hit->v[1][1] - hit->v[0][1];

        ground = hit->v[0][1]
                 + ((double)cy * ny + (double)hit->normal[2] * ((double)a - dz)
                    + ((double)b - dx) * (double)hit->normal[0]) / ny;
    }

    if (c->f558 != 0) {
        c->f55c = 0;
        if (g_drivers[i].control != 2 || i == 0) {
            c->f560 = 0;
        }
        if (scene_obj_set_mesh(&g_car_objs[i], g_car_meshes[model], 1, i + 100, 0, -1, 0) != 0) {
            log_printf("Error while changing car mesh\n");
        }
        for (w = 0; w < 4; w++) {
            const s32 *src = g_wheel_meshes[4 * model + w];
            s32 *dst = g_driver_wheels[i][w];

            for (v = 0; v < g_wheel_info[4 * model + w].verts; v++) {
                dst[2 + 3 * v] = src[2 + 3 * v] + g_wheel_dx[4 * model + w];
                dst[3 + 3 * v] = src[3 + 3 * v];
                dst[4 + 3 * v] = src[4 + 3 * v] + g_wheel_dz[4 * model + w];
            }
        }
        c->f558 = 0;
    }

    c->x = mx;
    c->y = my;
    c->z = mz;
    c->dx = c->vy = c->dz = 0.0;
    c->prev_dx = c->f038 = c->prev_dz = 0.0;
    c->f108 = 0.0;
    c->heading = h;
    c->f100 = 0.0;
    c->f110 = 0.0;
    c->rear_x = c->x - (double)c->rear_axle * cos(c->heading);
    c->rear_z = c->z - (double)c->rear_axle * sin(c->heading);
    c->front_x = (double)c->front_axle * cos(c->heading) + c->x;
    c->front_z = (double)c->front_axle * sin(c->heading) + c->z;
    c->f058 = c->f060 = c->f078 = c->f080 = 0.0;
    for (w = 0; w < 4; w++) {
        c->surface[w] = -1;
    }
    c->f120 = -ground;
    c->f128 = c->f120;
    c->f270 = 1;
    c->steer = c->f348 = c->f34c = 0;
    c->gear = 1;
    g_drivers[i].f10 = 0;
    c->f350 = 0;
    c->f298 = c->f29c = 0;
    c->f118 = 0.0;
    c->deck = 0;
    c->landing = 0;
    c->rpm = 0.0;
    c->skid = 0;
    c->f33c = 0;
    c->steer = c->f348 = c->f34c = 0;
    c->f364 = g_road_order[plc].order;
    c->f368 = c->f364;
    c->rear_dir = c->slip = c->front_dir = c->front_slip = 0.0;
    c->steer_shown = c->f0b8 = c->f0c0 = 0.0;
    c->f584 = (c->f108 * 57.295779513082195 + c->f0b8) * 10.0;
    c->f58c = (c->f110 * 57.295779513082195 + c->f0c0) * 10.0;
    g_race_cams[i].fc0 = 0.0;

    p->heading = c->heading;
    p->yaw_rate = 0.0;
    p->x = c->x * 0.04596;
    p->z = c->z * 0.04596;
    p->vx = p->vz = 0.0;
    p->f088 = p->f090 = 0.0;
    p->speed = 0.0;
    p->steer = 0.0;
    p->brake = 0;
    race_place_car(i);
}

/* 0x43edf0
 * Counts up, by the frame's ticks, while the car faces between 120 and
 * 240 degrees off the road's direction (to 120), and back down
 * otherwise. The HUD shows WRONG WAY past 25. */
void race_wrong_way(int car, double dt)
{
    struct car *c = &g_cars[car];
    s32 seg = c->f364;
    double road, diff;

    if (seg >= 0) {
        road = g_track_links[seg].heading;
    } else {
        road = g_track_links[-seg].alt_heading;
    }
    road = angle_mod(road - HALF_PI, TWO_PI);
    diff = fabs(road - angle_mod(c->heading, TWO_PI));
    if (diff > 2.0943951023932 && diff < 4.1887902047864) {
        c->wrong_way = (s32)((double)c->wrong_way + dt);
        if (c->wrong_way > 120) {
            c->wrong_way = 120;
        }
    } else if (c->wrong_way > 0) {
        c->wrong_way = (s32)((double)c->wrong_way - dt);
    }
}
