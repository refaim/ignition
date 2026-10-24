/*
 * race/sound.c - the race's sounds: the banks it loads (0x41f9b0),
 * sounds placed in the world and heard from the camera's car
 * (0x43e710), and the engines', tyres' and ghost's loops changed each
 * step (0x4452c0, 0x444fa0).
 *
 * The banks: 0 the general sounds (group 0 ROLL, 1 SKID, 2 COLL, 3
 * BOOST, 4 DIV), 1 the track's, 2 one group a car. A sound is as loud
 * as 1 - distance / 1500 of the way from the listener, the camera's car
 * or, once the players have finished, the camera over the finish; with
 * two players the nearer of the two cars.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "lisa/file.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/contact.h"
#include "race/data.h"
#include "race/init.h"
#include "race/sound.h"
#include "scene.h"
#include "../sound.h"

extern s32 r_527f20, r_54f958;

static s32 s_open_params[3];                /* 0x5530d0: channels, then two from the settings */
static struct sound_request s_request;      /* 0x5532d0 */
s32 g_ghost_voice;                          /* 0x603ad8 */
extern struct scene_desc *g_ghost_objs;     /* 0x525e40, race/misc.c (the ghost's load, 0x420240) */

/* The players are done: the one player, or both of two. */
static int players_finished(void)
{
    if (g_two_players == 0) {
        return g_cars[0].f528 == 1;
    }
    return g_two_players == 1 && g_cars[0].f528 == 1 && g_cars[1].f528 == 1;
}

/* The track's camera over the finish now in use. */
static const float *finish_cam(void)
{
    return &g_finish_cams[(g_track * 3 + r_563d20) * 5];
}

/* Where the sound is heard from: the camera over the finish once the
 * players are done (`strict` also wants it not the TV camera, nor the
 * player on autopilot), else the camera's car. */
static void listener(int strict, double *lx, double *lz)
{
    if (!(r_54f980 < 120.0) && players_finished()
        && (!strict || (r_563d20 != 3 && g_cars[0].f604 != 1))) {
        const float *f = finish_cam();

        *lx = f[0];
        *lz = f[2];
    } else {
        *lx = g_cars[g_view_car].x;
        *lz = g_cars[g_view_car].z;
    }
}

static int second_listener(void)
{
    return g_two_players == 1 && r_552f58 == 0;
}

/* 1 - d / 1500, 0 at the least, for a squared distance the engine and
 * the ghost take as an integer first. */
static double near_int(s32 d2)
{
    double f = d2 != 0 ? (1500.0 - sqrt((double)d2)) * 0.0006666666666666666 : 1.0;

    return 0.0 <= f ? f : 0.0;
}

/* The second player's car as a listener, for the engine and the ghost:
 * no 0.8, and a whole 1 when on it. */
static double near_second_int(double x, double z)
{
    double dx = fabs(x - g_cars[1].x), dz = fabs(z - g_cars[1].z);
    s32 d2 = (s32)(dx * dx + dz * dz);

    return d2 != 0 ? (1500.0 - sqrt((double)d2)) * 0.0006666666666666666 : (double)((1500 - d2) / 1500);
}

/* 0x43e710
 * Starts sample `sample` of bank a, group b, `loops` times (-1 for
 * ever), at `volume` scaled by how near (x, z) is, `rate` Hz (0 the
 * sample's own) and `pan`. Returns the voice's handle, 0 for none. */
s32 race_sound_3d(int bank, int group, int sample, int loops, int volume, int rate, int pan,
                  float x, float z)
{
    double lx, lz, dx, dz, d2, f;

    listener(0, &lx, &lz);
    dx = fabs(x - lx);
    dz = fabs(z - lz);
    if (dx != 0.0 || dz != 0.0) {
        dx *= 0.8;
        dz *= 0.8;
    }
    d2 = dx * dx + dz * dz;
    f = (d2 == 0.0 ? 1500.0 - d2 : 1500.0 - sqrt(d2)) * 0.0006666666666666666;
    if (0.0 > f) {
        f = 0.0;
    }
    if (second_listener()) {
        double f2;

        dx = fabs(x - g_cars[1].x);
        dz = fabs(z - g_cars[1].z);
        if (dx != 0.0 || dz != 0.0) {
            dx *= 0.8;
            dz *= 0.8;
        }
        d2 = dx * dx + dz * dz;
        f2 = (d2 == 0.0 ? 1500.0 - d2 : 1500.0 - sqrt(d2)) * 0.0006666666666666666;
        if (f < f2) {
            f = f2;
        }
    }
    s_request.bank = bank;
    s_request.group = group;
    s_request.index = sample;
    s_request.loops = loops;
    s_request.volume = (s32)(volume * f);
    s_request.rate = rate;
    s_request.pan = pan;
    return sound_start(&s_request);
}

/* A sound group, or the race cannot go on. */
static void load_group(const char *dir, int bank, int group)
{
    if (!sound_load_group(dir, bank, group)) {
        log_printf("Error while loading sound\n");
        exit(1);
    }
}

/* 0x457980: a request back to nothing, at full volume. */
static void request_clear(struct sound_request *r)
{
    r->bank = r->group = r->index = r->loops = 0;
    r->rate = r->pan = 0;
    r->volume = 0x10000;
    r->keep = 0;
}

/* 0x41f9b0
 * Opens the sound for the race and loads its banks, each car's engine
 * table (ENGINE.INF: four tables of 200 by revs) and starts the loops
 * that play all race: each car's two engine sounds and two tyre
 * sounds, silent until the steps set them, and the ghost's engine. */
void race_load_sounds(void)
{
    char name[100];
    int i, k;

    s_open_params[0] = 10;
    s_open_params[1] = r_527f20;
    s_open_params[2] = (u16)r_54f958;
    if (!sound_open(s_open_params)) {
        /* The original exits here; without an audio device the race
         * goes on silent. */
        log_printf("Error while initiating sound\n");
    }
    snprintf(name, sizeof(name), "%sSOUND\\ROLL", "GENERAL\\");
    load_group(name, 0, 0);
    snprintf(name, sizeof(name), "%sSOUND\\SKID", "GENERAL\\");
    load_group(name, 0, 1);
    snprintf(name, sizeof(name), "%sSOUND\\COLL", "GENERAL\\");
    load_group(name, 0, 2);
    snprintf(name, sizeof(name), "%sSOUND\\BOOST", "GENERAL\\");
    load_group(name, 0, 3);
    snprintf(name, sizeof(name), "%sSOUND\\DIV", "GENERAL\\");
    load_group(name, 0, 4);
    snprintf(name, sizeof(name), "LEVELS\\%sSOUND", g_track_dir);
    load_group(name, 1, 0);
    for (i = 0; i < g_driver_count; i++) {
        const char *dir = g_car_dirs[g_drivers[i].car];
        struct car *c = &g_cars[i];
        u8 *inf;

        snprintf(name, sizeof(name), "%s%s\\SOUND", "CARS\\", dir);
        load_group(name, 2, i);
        snprintf(name, sizeof(name), "%s%s\\SOUND\\ENGINE.INF", "CARS\\", dir);
        inf = file_load(name);
        if (inf == NULL) {
            log_printf("Error while loading %s\n", name);
            exit(1);
        }
        for (k = 0; k < 200; k++) {
            c->engine_pitch0[k] = (s8)inf[k];
            c->engine_loud0[k] = (s8)inf[k + 200];
            c->engine_pitch1[k] = (s8)inf[k + 400];
            c->engine_loud1[k] = (s8)inf[k + 600];
        }
        mem_free(0, inf);
    }
    request_clear(&s_request);
    sound_set_volume(g_race_sfx_volume * 655);
    for (i = 0; i < g_driver_count; i++) {
        struct car *c = &g_cars[i];

        c->engine_voice[0] = race_sound_3d(2, i, 0, -1, 0x10000, 5000, 0,
                                           (float)(s32)c->rear_x, (float)(s32)c->rear_z);
        c->engine_voice[1] = race_sound_3d(2, i, 1, -1, 0x10000, 5000, 0,
                                           (float)(s32)c->rear_x, (float)(s32)c->rear_z);
        c->tyre_voice[0] = race_sound_3d(0, 0, 0, -1, 0, 5000, 0, 0.0f, 0.0f);
        c->tyre_voice[1] = race_sound_3d(0, 0, 1, -1, 0, 5000, 0, 0.0f, 0.0f);
    }
    if (r_553068 == 1) {
        g_ghost_voice = race_sound_3d(0, 4, 4, -1, 0x5000, 0x55f0, 0, 5000.0f, 5000.0f);
    }
}

static int is_out(const struct car *c)
{
    return c->f354 || c->f358 || c->f35c;
}

static void set_voice(s32 handle, s32 rate, s32 volume)
{
    struct sound_voice *v = sound_voice_of(handle);

    if (v != NULL) {
        if (rate >= 0) {
            v->rate = rate;
        }
        v->volume = volume;
    }
}

/* The ghost's engine: as loud as the ghost is near. */
static void ghost_sound(const struct car *c)
{
    double lx, lz, gx, gz, dx, dz, f;

    if (is_out(c)) {
        set_voice(g_ghost_voice, -1, 0);
        set_voice(g_ghost_voice, -1, 0);
        return;
    }
    if (g_ghost_objs == NULL) {
        return;
    }
    gx = g_ghost_objs[0].x;
    gz = g_ghost_objs[0].z;
    listener(0, &lx, &lz);
    dx = fabs(gx - lx);
    dz = fabs(gz - lz);
    f = near_int((s32)(dx * dx + dz * dz));
    if (g_two_players == 1) {
        double f2 = near_second_int(gx, gz);

        if (f < f2) {
            f = f2;
        }
    }
    set_voice(g_ghost_voice, -1, (s32)(f * 35000.0));
}

/* 0x4452c0
 * The current car's engine (r_5285c0): its two loops follow the revs,
 * pitch and loudness from the car's ENGINE.INF, as loud as the car is
 * near. A car out of the race is silent. Racing the ghost, its engine
 * too. */
void race_engine_sound(void)
{
    struct car *c = &g_cars[r_5285c0];

    if (is_out(c)) {
        set_voice(c->engine_voice[0], -1, 0);
        set_voice(c->engine_voice[1], -1, 0);
    } else {
        double lx, lz, dx, dz, f;
        s32 idx;

        listener(1, &lx, &lz);
        dx = fabs(c->x - lx);
        dz = fabs(c->z - lz);
        if (dx != 0.0 || dz != 0.0) {
            dx *= 0.8;
            dz *= 0.8;
        }
        f = near_int((s32)(dx * dx + dz * dz));
        if (second_listener()) {
            double f2 = near_second_int(c->x, c->z);

            if (f < f2) {
                f = f2;
            }
        }
        idx = (s32)c->rpm * 2;
        if (idx > 199) {
            idx = 199;
        }
        if (idx < 1) {
            idx = 1;
        }
        /* The loudness is read one entry before the pitch. */
        set_voice(c->engine_voice[0], c->engine_pitch0[idx] * 600 + 20000,
                  (s32)((double)c->engine_loud0[idx - 1] * f * 650.0));
        set_voice(c->engine_voice[1], c->engine_pitch1[idx] * 600 + 20000,
                  (s32)((double)c->engine_loud1[idx - 1] * f * 650.0));
    }
    if (r_553068 == 1 && g_race_mode == 2) {
        ghost_sound(c);
    }
}

/* 0x444fa0
 * The current car's tyres on the road: the loop for its rear wheels'
 * surface (0 or 1; the others have none) pitched and as loud as its
 * speed. The original's distance comes out as a whole 0 or 1, so only
 * the camera's own car is heard. */
void race_tyre_sound(void)
{
    struct car *c = &g_cars[r_5285c0];
    double lx, lz;
    s32 ax, az, d2, near, rate, volume, base, k;
    int j;

    if (is_out(c) || c->f270 != 0) {
        set_voice(c->tyre_voice[0], 1000, 0);
        set_voice(c->tyre_voice[1], 1000, 0);
        return;
    }
    listener(1, &lx, &lz);
    ax = (s32)fabs(c->x - lx);
    az = (s32)fabs(c->z - lz);
    if (ax != 0 || az != 0) {
        ax = (s32)(ax * 0.8);
        az = (s32)(az * 0.8);
    }
    d2 = ax * ax + az * az;
    near = d2 != 0 ? (s32)((1500.0 - sqrt((double)d2)) * 0.0006666666666666666) : (1500 - d2) / 1500;
    if (near < 0) {
        near = 0;
    }
    if (second_listener()) {
        s32 e = (s32)fabs(c->x - g_cars[1].x), n2;

        /* The original takes only x here, twice. */
        d2 = e * e * 2;
        n2 = d2 != 0 ? (s32)((1500.0 - sqrt((double)d2)) * 0.0006666666666666666) : (1500 - d2) / 1500;
        if (near < n2) {
            near = n2;
        }
    }
    k = (s32)fmod((double)c->surface[2], 10.0);
    if (k == 9) {
        k = 4;
    }
    /* No surface (-1) reads the 0 before the table. */
    base = k >= 0 ? g_tyre_pitch[k] : 0;
    rate = (s32)((double)base + c->f118 * 100.0);
    volume = (s32)((double)(near * base) * c->f118 * 0.01);
    if (volume > 0x10000) {
        volume = 0x10000;
    }
    for (j = 0; j < 2; j++) {
        set_voice(c->tyre_voice[j], rate, k == j ? volume : 0);
    }
}

/* One exhaust puff from the current car, to one side (`side` 22 or
 * -22), drifting with the car. */
static void exhaust_puff(const struct car *c, s32 side)
{
    g_effect.type = 1;
    g_effect.x = (s32)(c->x * 1024.0);
    g_effect.y = (s32)(c->y * 1024.0);
    g_effect.z = (s32)(c->z * 1024.0);
    g_effect.f10 = (s32)(c->dx * 921.6 + (race_rand() * 1024.0 - 512.0));
    g_effect.f14 = 0x400;
    g_effect.f18 = (s32)((race_rand() * 1024.0 - 512.0) + c->dz * 921.6);
    g_effect.f1c = 0x400;
    g_effect.f20 = 0;
    g_effect.f24 = 5;
    g_effect.f28 = 0x1000;
    g_effect.f2c = 0x3d;
    g_effect.f30 = 0x19000;
    g_effect.f34 = 0x1000;
    g_effect.f38.frames = g_exhaust_frames;
    g_effect.f3c = -25;
    g_effect.f40 = -50;
    g_effect.f44 = side;
    g_effect.f48 = (s32)(c->f108 * 572.957795130822);
    g_effect.f4c = (s32)(c->heading * 572.957795130822);
    g_effect.f50 = (s32)(c->f110 * 572.957795130822);
    g_effect.f54 = 0;
    g_effect.f58 = 0x32;
    race_effect(&g_effect);
}

/* 0x444450
 * Car 8's exhaust (named with the sounds, but smoke): every third tick,
 * with the throttle on, two puffs, each likelier the slower the car. */
void race_exhaust(void)
{
    const struct car *c = &g_cars[r_5285c0];
    const struct car_phys *p = &g_car_phys[r_5285c0];

    if ((s32)fmod(g_race_clock, 3.0) != 0) {
        return;
    }
    if (p->speed * 3.6 < race_rand() * 160.0 && c->throttle_on == 1) {
        exhaust_puff(c, 0x16);
    }
    if (p->speed * 3.6 < race_rand() * 160.0 && c->throttle_on == 1) {
        exhaust_puff(c, -0x16);
    }
}
