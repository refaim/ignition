/*
 * race_contact.c - a car on the ground (0x424570) and what touching it
 * sets off: laps and records, trigger zones (reset, crash roll, bump,
 * squash), being knocked out of an elimination race (0x423620), and the
 * squashed car's mesh (0x4269a0).
 *
 * Each step probes the .SRF under the car's four corners (scaled by the
 * speed) for their surface and height. The mean height is the ground,
 * the front against the rear the roll (0x110) and the sides the pitch
 * (0x108). Walls under a corner flag the wall collision; if a corner is
 * off the road the corners are probed again at half size.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "lisa/log.h"
#include "race.h"
#include "race_cars.h"
#include "race_ai.h"
#include "race_contact.h"
#include "race_data.h"
#include "race_init.h"
#include "scene.h"
#include "srf.h"
#include "stubs.h"

/* ---- Defined elsewhere without a header declaration yet. ---- */
extern s32 *r_552f24;               /* race_init.c: the messages, 5 of 0x78 bytes */
extern s32 r_5285d0;                /* race_init.c: 1 probes the objects for zones too */
extern s32 r_527f24;                /* race_init.c: cars still in the race */
extern s32 r_527f40[11];            /* race_init.c */
extern s32 r_54f9a8;                /* race_init.c: the car the host knocked out */
extern double r_54f980;             /* race_init.c */
extern double r_553090;             /* race.c: the race's time in seconds */
extern s32 g_cd_last_track;         /* game.c, 0x4949e0 */
/* ---- */

struct race_effect g_effect;
s32 r_563d20, r_4949a8;
s32 r_619300;
s32 g_squash_meshes[11][2000];

/* A message on the screen (0x552f24, 0x78 bytes). */
enum {
    SEG_NONE = 10000,
    LANG_COUNT = 6
};

static const char *const s_lap_record[LANG_COUNT] = {
    "LAP RECORD", "RUNDENREKORD", "RECORD SUL GIRO", "RECORD DE VUELTA",
    "VARV REKORD", "RECORD DU TOUR"
};
static const char *const s_track_record[LANG_COUNT] = {
    "TRACK RECORD", "STRECHENREKORD", "RECORD DELLA PISTA", "RECORD DE PISTA",
    "BAN REKORD", "RECORD DU CIRCUIT"
};
static const char *const s_is_out[LANG_COUNT] = {
    "%s IS OUT", "%s IST AUSGESCHIEDEN", "%s \xc8 FUORI", "%s ESTA FUERA",
    "%s bR UTE", "%s EST ELIMINE"
};

/* 0x446180 */
float race_rand(void)
{
    float r = (float)((race_msrand() & 0x3fff) * 6.103515625e-05);

    if (r < 0.0f || r > 1.0f) {
        log_printf("FLUM SLUMP!!\n");
    }
    return r;
}

/* 0x434380 */
void race_effect(struct race_effect *e)
{
    (void)e;
    STUB(0x434380, "race_effect");
}

/* 0x441170 */
int race_message(const char *text, int a, int b, int view)
{
    struct race_msg *m = (struct race_msg *)r_552f24;
    int i;

    for (i = 0; i < 5 && m[i].active; i++) {
    }
    if (i == 5) {
        return -1;
    }
    m[i].active = 1;
    /* sprintf in the original, unbounded. */
    snprintf(m[i].text, sizeof(m[i].text), "%s", text);
    m[i].life = a;
    m[i].blink = b;
    m[i].age = 0;
    m[i].view = view;
    return i;
}

/* A player's message, on their half of the screen with two players. */
static void player_message(int car, const char *text)
{
    if (car == 0 && g_two_players == 0) {
        race_message(text, 80, 8, -1);
    } else if (car == 0 && g_two_players == 1) {
        race_message(text, 80, 8, 0);
    } else if (car == 1 && g_two_players == 1) {
        race_message(text, 80, 8, 1);
    }
}

static int language(void)
{
    return race_cfg(0x593);
}

/* The sound at a car, its place cut to whole units. */
static void car_sound(const struct car *c, int a, int b, int sample)
{
    float z = (float)(s32)c->z;
    float x = (float)(s32)c->x;

    race_sound_3d(a, b, sample, 0, 0x10000, 0x55f0, 0, x, z);
}

/* An explosion (3), splash (7) or fall (10) at the car's rear axle. */
static void car_effect(int car, int type)
{
    const struct car *c = &g_cars[car];

    g_effect.type = type;
    g_effect.x = (s32)(c->rear_x * 1024.0);
    g_effect.y = (s32)(c->y * 1024.0);
    g_effect.z = (s32)(c->rear_z * 1024.0);
    g_effect.f10 = car;
    g_effect.f14 = 0;
    g_effect.f18 = 80;
}

/* 0x4269a0
 * Flattens the car: its mesh copied to the car's own buffer, scaled 1.2
 * wide, 0.2 high and 1.6 long. */
void race_squash(int car)
{
    const s32 *src = g_car_meshes[g_drivers[car].car];
    s32 *dst = g_squash_meshes[car];
    int n, i;

    /* Header, vertices and faces: count verts * 3 + faces * 11 + 2. */
    n = src[0] * 3 + src[1] * 11 + 2;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    for (i = 0; i < src[0]; i++) {
        dst[2 + 3 * i] = (s32)(src[2 + 3 * i] * 1.2);
        dst[3 + 3 * i] = (s32)(src[3 + 3 * i] * 0.2);
        dst[4 + 3 * i] = (s32)(src[4 + 3 * i] * 1.6);
    }
    if (scene_obj_set_mesh(&g_car_objs[car], dst, 1, car + 100, 0, -1, 0) != 0) {
        log_printf("Error while changing car mesh\n");
    }
    car_sound(&g_cars[car], 0, 2, 6);
}

/* 0x423620
 * Elimination: when one car still racing is a lap behind the others it
 * is out (with the network, the host says which). It blows up and the
 * race goes on; when one car is left it has won. */
void race_knockout(void)
{
    char buf[100] = "";
    struct car *c;
    int i, max = 0, behind = 0, out = 0;

    for (i = 0; i < g_driver_count; i++) {
        if (g_cars[i].f528 == 0 && g_cars[i].laps > max) {
            max = g_cars[i].laps;
        }
    }
    for (i = 0; i < g_driver_count; i++) {
        if (g_cars[i].f528 == 0 && g_cars[i].laps < max) {
            behind++;
            out = i;
        }
    }
    if (!(behind == 1 && r_552f10 != 4) && !(r_54f97c == 3 && r_552f10 == 4)) {
        return;
    }
    if (r_54f97c == 3 && r_552f10 == 4) {
        out = r_54f9a8;
    }
    c = &g_cars[out];
    if (c->f60c != 0.0) {
        return;
    }
    log_printf("BIL %d RACEEND %d BLOW %.3f", out, c->f528, c->f60c);
    c->f354 = 1;
    car_effect(out, 3);
    g_effect.f1c = 1;
    race_effect(&g_effect);
    c->f60c = 1.0;
    race_sound_3d(0, 2, 4, 0, 0xc000, 0x55f0, 0, (float)c->x, (float)c->z);
    c->f528 = 1;
    c->f3a0 = c->grid;
    if ((g_two_players == 0 && g_cars[0].f528 == 1)
        || (g_two_players == 1 && g_cars[0].f528 == 1 && g_cars[1].f528 == 1)) {
        if (r_54f980 < 0.0) {
            r_54f980 = 0;
            r_527f40[0] = 0;
            r_527f40[1] = 0;
        }
    }
    if (language() >= 0 && language() < LANG_COUNT) {
        sprintf(buf, s_is_out[language()], g_drivers[out].name);
    }
    if (g_two_players == 0) {
        race_message(buf, 80, 8, -1);
    } else {
        race_message(buf, 80, 8, 0);
        race_message(buf, 80, 8, 1);
    }
    r_553280++;
    r_527f24--;
    if (r_527f24 == 1) {
        for (i = 0; i < g_driver_count; i++) {
            if (g_cars[i].f528 == 0) {
                g_cars[i].f528 = 1;
                g_cars[i].f3a0 = g_cars[i].grid;
                if (r_54f980 < 0.0) {
                    r_54f980 = 0;
                    r_527f40[i] = 0;
                }
                r_553280 = g_driver_count;
            }
        }
    }
    if (r_552f10 == 3) {
        r_54f97c = 3;
        r_54f9a8 = out;
    }
    if (out == 0 || (out == 1 && g_two_players == 1)) {
        r_563d20 = 3;
        r_4949a8 = 100;
        r_54f980 = 0;
        r_527f40[out] = 0;
    }
}

/* The height of a face's plane at (x, z); y is negated in the SRF. */
static double plane_height(const struct srf_hit *h, double x, double z)
{
    double tz = (double)(h->v[1][2] - h->v[0][2]) - (z - h->v[0][2]);
    double tx = (double)(h->v[1][0] - h->v[0][0]) - (x - h->v[0][0]);
    double s = tz * h->normal[2] + tx * h->normal[0]
               + (double)(h->v[1][1] - h->v[0][1]) * h->normal[1];

    if (h->normal[1] != 0) {
        s /= h->normal[1];
    }
    return h->v[0][1] + s;
}

/* Surfaces from 0x50 are walls. Some only count above the ground's
 * level or off the upper deck. */
static int is_wall(s32 t, int low, int deck)
{
    switch (t) {
    case 0x50: case 0x5a: case 0x5b:
        return 1;
    case 0x51: case 0x5c: case 0x5d:
        return !low;
    case 0x6e: case 0x78: case 0x79: case 0x82: case 0x83: case 0x8c: case 0x8d:
        return !deck;
    case 0x6f:
        return !low && !deck;
    default:
        return 0;
    }
}

/* The deck the car is on follows the materials under it: 10..19 the
 * upper deck, 0..9 the lower unless the corner is above the ground. */
static void update_deck(struct car *c, s32 t, int low)
{
    if (t >= 10 && t < 20) {
        c->deck = 1;
    } else if (t >= 0 && t < 10 && !low) {
        c->deck = 0;
    }
}

/* A corner's probe point, the corner rotated by the heading and scaled
 * by `scale` (the speed's, halved on the narrow probe). */
static void corner_point(const struct car *c, int k, int half, double *px, double *pz)
{
    double cs = cos(c->heading), sn = sin(c->heading);
    double rx = c->corner_x[k] * cs - c->corner_z[k] * sn;
    double rz = c->corner_x[k] * sn + c->corner_z[k] * cs;

    if (half) {
        *px = rx * c->probe_scale * 0.5 + c->x;
        *pz = rz * c->probe_scale * 0.5 + c->z;
    } else {
        *px = rx * c->probe_scale + c->x;
        *pz = rz * c->probe_scale + c->z;
    }
}

/* The ground's height at a corner, with Japan's (track 6) segments
 * 0xc2..0xc8 where a corner's face is far from the ground held to it. */
static double corner_height(const struct car *c, const struct srf_hit *h, double px, double pz)
{
    double y = plane_height(h, px, pz);

    if (fabs(y + c->f120) > 5.0 && g_track == 6 && c->f364 > 0xc1 && c->f364 < 0xc9) {
        y = -c->f120;
    }
    return y;
}

static void lap_done(int car)
{
    struct car *c = &g_cars[car];
    char buf[100] = "";
    double record;

    c->laps++;
    c->f378 = 0;
    if (c->laps >= 3 && g_race_mode != 3) {
        c->f528 = 1;
        c->f3a0 = c->grid;
        r_553280++;
        if (car == 0 || (car == 1 && g_two_players == 1)) {
            r_54f980 = 0;
            r_563d20 = 3;
            r_4949a8 = 100;
            r_527f40[car] = 0;
        }
    }
    c->f3b4 = c->lap_time;
    /* Each lap's time at 0x3b4 + 8 * lap. */
    switch (c->laps) {
    case 0:
        c->f3b4 = c->lap_time;
        break;
    case 1:
        c->f3bc = c->lap_time;
        break;
    case 2:
        c->f3c4 = c->lap_time;
        break;
    case 3:
        c->f3cc = c->lap_time;
        break;
    }
    c->lap_start = r_553090;
    if (c->f3b4 < g_best_lap) {
        g_best_lap = c->f3b4;
        if (language() >= 0 && language() < LANG_COUNT) {
            sprintf(buf, "%s", s_lap_record[language()]);
        }
        player_message(car, buf);
    }
    memcpy(&record, g_race_cfg + 0x1d3 + 120 * g_track, sizeof(record));
    if (c->laps == 3 && g_race_mode != 3 && record > c->f3a4) {
        if (language() >= 0 && language() < LANG_COUNT) {
            sprintf(buf, "%s", s_track_record[language()]);
        }
        player_message(car, buf);
    }
}

/* The squashed car is put back once no other car is on it. */
static void unsquash(int car)
{
    struct car *c = &g_cars[car];
    const struct race_driver *d = &g_drivers[car];
    int j, w, v;

    for (j = 0; j < g_driver_count; j++) {
        if (fabs(c->x - g_cars[j].x) < 30.0 && fabs(c->z - g_cars[j].z) < 30.0 && j != car) {
            c->f558 = 200;
            return;
        }
    }
    c->f55c = 0;
    if (!(d->control == 2 && car != 0)) {
        c->f560 = 0;
    }
    if (scene_obj_set_mesh(&g_car_objs[car], g_car_meshes[d->car], 1, car + 100, 0, -1, 0) != 0) {
        log_printf("Error while changing car mesh\n");
        exit(1);
    }
    for (w = 0; w < 4; w++) {
        int m = d->car * 4 + w;
        const s32 *src = g_wheel_meshes[m];
        s32 *dst = &g_driver_wheels[car][w][2];

        for (v = 0; v < g_wheel_info[m].verts; v++) {
            dst[3 * v] = g_wheel_dx[m] + src[2 + 3 * v];
            dst[3 * v + 1] = src[3 + 3 * v];
            dst[3 * v + 2] = g_wheel_dz[m] + src[4 + 3 * v];
        }
    }
    c->f558 = 0;
    if (c->f270 == 0) {
        c->vy -= 6.0;
    }
    car_sound(c, 0, 2, 7);
}

/* A puff of smoke off a squashed car, one step in six. */
static void squash_smoke(const struct car *c)
{
    if ((s32)(race_rand() * 6.0) != 0) {
        return;
    }
    g_effect.type = 1;
    g_effect.x = (s32)((race_rand() * 50.0 + c->x - 25.0) * 1024.0);
    g_effect.y = (s32)((c->y + 10.0) * 1024.0);
    g_effect.z = (s32)((race_rand() * 50.0 + c->z - 25.0) * 1024.0);
    g_effect.f10 = (s32)((race_rand() - 0.5) * 1024.0);
    g_effect.f14 = (s32)((race_rand() + 1.0) * 1024.0);
    g_effect.f18 = (s32)((race_rand() - 0.5) * 1024.0);
    g_effect.f20 = 0;
    g_effect.f1c = 0x400;
    g_effect.f24 = 5;
    g_effect.f28 = 0xc00;
    g_effect.f2c = 0x66;
    g_effect.f30 = (s32)((race_rand() * 150.0 + 150.0) * 1024.0);
    g_effect.f34 = (s32)((race_rand() * 3.0 + 3.0) * 1024.0);
    g_effect.f3c = g_effect.f40 = g_effect.f44 = 0;
    g_effect.f38.frames = g_smoke_frames;
    g_effect.f48 = g_effect.f4c = g_effect.f50 = g_effect.f54 = 0;
    g_effect.f58 = 50;
    race_effect(&g_effect);
}

/* A piece of the car flying off at the start of a crash roll. */
static void roll_debris(const struct car *c)
{
    g_effect.type = 5;
    g_effect.x = (s32)(c->x * 1024.0);
    g_effect.y = (s32)(c->y * 1024.0);
    g_effect.z = (s32)(c->z * 1024.0);
    g_effect.f10 = (s32)((race_rand() * 4.0 - 2.0) * 1024.0);
    g_effect.f14 = (s32)((race_rand() * 3.0 + 3.0) * 1024.0);
    g_effect.f18 = (s32)((race_rand() * 4.0 - 2.0) * 1024.0);
    g_effect.f24 = g_effect.f28 = g_effect.f2c = 0;
    g_effect.f1c = 0x400;
    g_effect.f20 = -460;
    g_effect.f30 = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
    g_effect.f34 = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
    g_effect.f38.i = (s32)((race_rand() * 100.0 - 50.0) * 1024.0);
    g_effect.f3c = g_effect.f44 = g_effect.f48 = 0;
    g_effect.f40 = 1;
    g_effect.f4c = 30;
    race_effect(&g_effect);
}

/* Clamps a new angle to within 0.1 of the old. */
static double ease_angle(double a, double old)
{
    if (a - old > 0.1) {
        a = old + 0.1;
    }
    if (a - old < -0.1) {
        a = old - 0.1;
    }
    return a;
}

static double clamp95(double s)
{
    if (s > 0.95) {
        s = 0.95;
    }
    if (s < -0.95) {
        s = -0.95;
    }
    return s;
}

/* 0x424570 */
void race_contact(int car)
{
    struct car *c = &g_cars[car];
    struct car_phys *p = &g_car_phys[car];
    const struct race_driver *d = &g_drivers[car];
    struct srf_hit rec[4];
    const struct srf_hit *hit, *zone = NULL;
    double h[4], h2[4], px, pz, lim;
    double ground = 0, front = 0, rear = 0, side_a = 0, side_b = 0;
    double pitch, roll;
    int ground_n = 0, reset = 0, event = 0, trig = 0, low = 0;
    int k, i, n, changed, count;
    s32 seg, lane, t;

    for (k = 0; k < 4; k++) {
        c->last_surface[k] = c->surface[k];
        c->surface[k] = -1;
    }
    for (k = 0; k < 4; k++) {
        corner_point(c, k, 0, &px, &pz);
        hit = srf_lookup((s32)px, (s32)(c->y + 50.0), (s32)pz);
        if (k == 0) {
            /* Trigger zones under the car's middle. */
            trig = 0;
            if (r_5285d0 == 1) {
                zone = race_obj_query((s32)c->x, (s32)c->y, (s32)c->z, -1, 50);
                if (zone->type == 301) {
                    event = 1;
                }
            }
            /* The original tests the distance one way only: < 200 in
             * x and z. */
            for (i = 0; i < r_552f78; i++) {
                const struct scene_desc *o = &g_world[g_movers[i].plc];
                double dx = c->x - o->x, dz = c->z - o->z;

                if (!(dx < 200.0) || !(dz < 200.0)) {
                    continue;
                }
                if (g_track != 5 || plc_at(g_level_plc, g_movers[i].plc)->type != 300) {
                    zone = race_zone_query((s32)c->x, (s32)c->y, (s32)c->z, car);
                    if (zone->type >= 300) {
                        trig = 1;
                    }
                    break;
                }
                if (fabs(dx) < 30.0 && fabs(c->y - o->y) < 30.0 && fabs(dz) < 30.0) {
                    reset = 1;
                }
            }
        }
        if (trig == 1) {
            switch (zone->type) {
            case 300:
                reset = 1;
                break;
            case 301:
                event = 1;
                break;
            case 302:
                event = 2;
                break;
            case 303:
                event = 3;
                break;
            case 304:
                reset = 3;
                break;
            case 305:
                for (i = 0; i < r_552f78; i++) {
                    const struct scene_desc *o = &g_world[g_movers[i].plc];

                    if (fabs(o->x - c->x) < 80.0 && fabs(o->z - c->z) < 80.0
                        && g_movers[i].f0c > 0) {
                        event = 1;
                    }
                }
                break;
            }
        }
        /* A lap: the last quarter done and over the line (segment 0)
         * from the last piece. */
        if (c->f378 == 3 && hit->f20 == 0 && abs(c->f370) - g_track_link_count == -1
            && c->f604 == 0) {
            lap_done(car);
        }
        if (hit->f20 != SEG_NONE) {
            c->f370 = hit->f20;
        }

        h[k] = corner_height(c, hit, px, pz);
        if (k >= 2) {
            rear += h[k];
        } else {
            front += h[k];
        }
        if (k == 0 || k == 3) {
            side_a += h[k];
        } else {
            side_b += h[k];
        }
        rec[k] = *hit;
        t = rec[k].type;
        c->surface[k] = t;
        low = 20.0 - h[k] <= c->y;
        c->wall_surface[k] = t;
        if (t < 0x50) {
            update_deck(c, t, low);
        } else if (is_wall(t, low, c->deck)) {
            c->wall_contact = 1;
            /* Low walls the car rides against do not count for the
             * ground's height. */
            if (t == 0x51 || t == 0x5c || t == 0x6f) {
                continue;
            }
        }
        ground -= h[k];
        ground_n++;
    }

    /* The segment: the front corners', when both agree and the car is
     * on the road. rec[3] is the last probe's record. */
    changed = 0;
    seg = rec[0].f20;
    lane = rec[0].klass;
    if (c->f270 == 1 || c->landing != 0 || rec[3].type == -1 || rec[0].f20 != rec[1].f20
        || rec[0].type >= 100 || c->f350 == 1) {
        seg = c->f364;
        lane = c->f36c;
    }
    if (seg != SEG_NONE) {
        if (c->f364 != seg) {
            changed = 1;
            c->f368 = c->f364;
        }
        c->f364 = seg;
        c->f36c = lane;
    }
    if (changed) {
        c->seg_frames = 0;
    } else if (c->seg_frames < 29) {
        c->seg_frames++;
    }
    /* Quarters of the lap, so a lap can not be cut short. */
    n = g_track_link_count / 4;
    i = abs(c->f364);
    if (c->f378 == 0) {
        if (i >= n && 2 * n > i) {
            c->f378 = 1;
        }
    } else if (c->f378 == 1) {
        if (2 * n <= i && 3 * n > i) {
            c->f378 = 2;
        }
    } else if (c->f378 == 2) {
        if (3 * n <= i && 4 * n > i) {
            c->f378 = 3;
        }
    }
    if (rec[0].type < 0 && rec[1].type < 0 && rec[2].type < 0 && rec[3].type < 0) {
        reset = 1;
    }
    if (rec[0].type > 40 && rec[1].type > 40 && rec[2].type > 40 && rec[3].type > 40
        && c->f270 == 0) {
        c->off_track++;
        if (c->off_track > 5) {
            reset = 1;
        }
    } else {
        c->off_track = 0;
    }

    c->narrow = 0;
    if (rec[0].type > 19 || rec[1].type > 19 || rec[2].type > 19 || rec[3].type > 19) {
        /* Off the road: probe again at half size. `low` is the last
         * corner's from the first pass, as in the original. */
        ground = front = rear = side_a = side_b = 0;
        c->narrow = 1;
        ground_n = 4;
        for (k = 0; k < 4; k++) {
            corner_point(c, k, 1, &px, &pz);
            hit = srf_lookup((s32)px, (s32)(c->y + 50.0), (s32)pz);
            h[k] = corner_height(c, hit, px, pz);
            if (k >= 2) {
                rear += h[k];
            } else {
                front += h[k];
            }
            if (k == 0 || k == 3) {
                side_a += h[k];
            } else {
                side_b += h[k];
            }
            rec[k] = *hit;
            c->surface[k] = rec[k].type;
            update_deck(c, rec[k].type, low);
            ground -= h[k];
        }
    }
    if (rec[0].type < 0 && rec[1].type < 0 && rec[2].type < 0 && rec[3].type < 0) {
        reset = 1;
    }
    for (k = 0; k < 4; k++) {
        if (rec[k].type < 0) {
            memcpy(&rec[k], &c->srf_cache[16 * k], sizeof(rec[k]));
        }
    }

    /* Corners far from the ground for the speed, or on a wall, take
     * the car's own height; unless all of them do. */
    count = 0;
    lim = c->f118 * 0.5 + 100.0;
    for (k = 0; k < 4; k++) {
        h2[k] = h[k];
        if (abs((s32)-(c->f120 + h[k])) > lim || rec[k].type >= 0x50) {
            h2[k] = c->vy - c->f120;
            count++;
        }
    }
    if (count > 0 && count < 4) {
        ground = front = rear = side_a = side_b = 0;
        for (k = 0; k < 4; k++) {
            h[k] = h2[k];
            ground -= h[k];
            if (k >= 2) {
                rear += h[k];
            } else {
                front += h[k];
            }
            if (k == 0 || k == 3) {
                side_a += h[k];
            } else {
                side_b += h[k];
            }
        }
    }
    if (ground * 0.25 > 1800.0 || ground * 0.25 < -1800.0) {
        g_cd_last_track = 0;
    }

    /* Grip from the surfaces, the car and the difficulty. */
    if (c->surface[0] >= 0 && c->surface[0] < 30 && c->surface[1] >= 0 && c->surface[1] < 30
        && c->surface[2] >= 0 && c->surface[2] < 30 && c->surface[3] >= 0 && c->surface[3] < 30) {
        c->f148 = (g_surface_grip[c->surface[3]] + g_surface_grip[c->surface[2]]
                   + g_surface_grip[c->surface[0]] + g_surface_grip[c->surface[1]]) * 0.25;
        if (d->control != 1) {
            p->traction = c->f148 * g_car_traction[d->car];
            p->grip = g_car_traction[d->car] * c->f148 * g_grip_scale[g_mirror];
        } else {
            p->traction = c->f148 * g_car_traction[11];
            p->grip = g_ai_grip[g_car_499a90[d->car] + g_mirror * 11] * c->f148
                      * g_car_traction[11];
        }
    }
    memcpy(c->srf_cache, rec, sizeof(rec));
    if (ground_n > 0) {
        ground /= ground_n;
    } else {
        ground = rec[0].v[0][1];
    }

    if (c->f270 == 0) {
        double div = c->narrow + 1;

        front *= 0.5;
        rear *= 0.5;
        side_b *= 0.5;
        side_a *= 0.5;
        roll = asin(clamp95((rear - front)
                            / (fabs((c->corner_x[0] - c->corner_x[3]) * c->probe_scale) / div)));
        roll = ease_angle(roll, c->f110);
        pitch = asin(clamp95((side_a - side_b) / (fabs(c->corner_z[0] - c->corner_z[1]) / div))
                     * -0.6);
        pitch = ease_angle(pitch, c->f108);
    } else {
        pitch = c->f108;
        roll = c->f110;
    }
    c->f128 = c->f120;
    c->f120 = ground;
    if ((c->narrow == 1 && c->last_narrow == 0) || (c->narrow == 0 && c->last_narrow == 1)) {
        c->f128 = ground;
    }
    c->last_narrow = c->narrow;
    if (c->f270 == 0 && c->f554 == 0) {
        c->f108 = pitch;
        c->f110 = roll;
    } else if (c->f554 > 0) {
        /* The crash roll: a turn over 80 steps. */
        c->f554++;
        c->f110 = fmod(c->f554 * 0.078, 6.2831853071796);
        if (c->f554 > 80) {
            c->f584 = c->f108 * 57.295779513082195 * 10.0;
            c->f58c = 0;
            c->f110 = 0;
            c->f554 = 0;
        }
    } else if (c->f110 < 0.78539816339745) {
        c->f110 += c->f130;
    }

    if (reset > 0) {
        if (reset == 1) {
            c->f354 = 1;
            car_effect(car, 3);
            g_effect.f1c = 1;
            race_effect(&g_effect);
        } else if (reset == 2) {
            c->f358 = 1;
            car_effect(car, 7);
            race_effect(&g_effect);
        } else if (reset == 3) {
            c->f35c = 1;
            car_effect(car, 10);
            race_effect(&g_effect);
        }
    }
    if (event > 0) {
        if (event == 1 && c->f270 == 0 && c->f554 == 0) {
            c->f554 = 1;
            c->vy -= 8.0;
            if (c->vy < -10.0) {
                c->vy = -10.0;
            }
            roll_debris(c);
        }
        if (event == 2 && c->f270 == 0 && fabs(c->y - c->f120) < 50.0) {
            /* A bump throws the car up and turns it. */
            c->vy = (race_rand() * -3.0 + c->vy) - 4.5;
            p->yaw_rate = (race_rand() * -1.5 + p->yaw_rate) - 2.5;
            c->f270 = 1;
            if (c->vy < -10.0) {
                c->vy = -10.0;
            }
            if (r_619300 == 0) {
                car_sound(c, 1, 0, 8);
                r_619300 = 15;
            }
        }
        if ((event == 3 && c->f558 == 0) || (c->f55c == 0 && c->f560 == 1)) {
            c->f55c = 1;
            if (!(d->control == 2 && car != 0)) {
                c->f560 = 1;
            }
            c->f558 = 1;
            race_squash(car);
        }
    }
    if (r_619300 > 0) {
        r_619300--;
    }
    if (c->f558 > 0) {
        c->f558++;
        if (c->f558 >= 200 || (c->f55c == 1 && c->f560 == 0)) {
            unsquash(car);
        }
        squash_smoke(c);
    }

    if (r_552f10 != 2) {
        if (reset > 0) {
            c->net_event = -reset;
        } else if (event > 0) {
            c->net_event = event;
        }
    }
}
