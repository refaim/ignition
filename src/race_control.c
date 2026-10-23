/*
 * race_control.c - a car's controls (0x442030..0x4452c0): the engine's
 * force and revs, the brake, the steering, the gearbox and the boost.
 *
 * Inputs run 0..100 (steer -100..100). The car is the one r_5285c0
 * names, except for the gearbox, which takes it.
 */

#include <math.h>
#include <stdlib.h>

#include "race_sound.h"
#include "race.h"
#include "race_cars.h"
#include "race_control.h"
#include "race_data.h"
#include "race_init.h"
#include "sound.h"
#include "stubs.h"

s32 r_563d58, r_563d5c;
s32 r_4949c0 = 1;                       /* 0x4949c0, never written */

extern s32 r_5531d0[0x2c];              /* the inputs each car last had */
extern double r_553090;                 /* the race's time in seconds */

/* The original's own roundings of pi / 2 and 3 pi / 2. */
#define HALF_PI 1.5707963267949
#define THREE_HALF_PI 4.7123889803847

static void sound_stop(s32 handle)
{
    struct sound_voice *v = sound_voice_of(handle);

    if (v != NULL) {
        v->rate = 0;
    }
}

/* A sound at the car's rear axle, at whole world units. */
static s32 sound_at_rear(const struct car *c, int b, int sample, int cc, int d)
{
    float x = (float)(s32)c->rear_x, z = (float)(s32)c->rear_z;

    return race_sound_3d(0, b, sample, cc, d, 0x55f0, 0, x, z);
}

/* Going forwards: the rear wheels travel within pi / 2 of the heading. */
static int forwards(double slip)
{
    return (slip > -HALF_PI && slip < HALF_PI) || slip > THREE_HALF_PI;
}

/* Past the finish, a car coasts to a stop, later the further back it
 * finished. */
static int coasting(const struct car *c)
{
    return c->f528 == 1 && c->f604 == 0
        && (g_driver_count - c->f3a0) * 0.4 + c->f3a4 < r_553090;
}

/* 0x442030
 * One step of the car's controls. accel RACE_CONTROL_REPEAT applies the
 * inputs stored for the car last time again. With the automatic gearbox
 * (the driver's f0c) the brake pedal drives in reverse; action 1 starts
 * the boost when it is full, 2 its standing-start form. */
void race_control(int accel, int brake, int steer, int action)
{
    int i = r_5285c0;
    s32 *mem = &r_5531d0[i * 4];
    struct car *c;
    struct race_driver *d = &g_drivers[i];

    r_563d58 = accel;
    r_563d5c = brake;
    if (accel == RACE_CONTROL_REPEAT) {
        accel = mem[0];
        brake = mem[1];
        steer = mem[2];
        action = mem[3];
    } else {
        mem[0] = accel;
        mem[1] = brake;
        mem[2] = steer;
        mem[3] = action;
    }
    c = &g_cars[i];
    if (c->gear > 0) {
        race_engine(accel, 1);
    } else {
        race_engine(-(d->f0c == 0 ? accel : brake), 0);
    }
    race_boost();
    if (d->car == 8) {
        race_exhaust();
    }
    race_tyre_sound();
    race_engine_sound();
    if (d->f0c == 0) {
        race_brake(brake);
    } else {
        race_brake(0);
        if (forwards(c->slip) && g_car_phys[i].speed > 2.0) {
            race_brake(brake);
        } else if (brake > 5) {
            if (c->gear != 0 && d->control != 1) {
                sound_at_rear(c, 4, 2, 0, 0xd000);
            }
            c->gear = 0;
        }
        if (accel > 50 && c->gear == 0) {
            if (d->control != 1) {
                sound_at_rear(c, 4, 2, 0, 0xd000);
            }
            c->gear = 1;
        }
    }
    race_steer(steer);
    if (action == 1) {
        if (c->f534 >= 100.0 && c->f53c == 0 && r_552e48 < 0.0 && c->f528 == 0) {
            c->f53c = 1;
            c->f548 = 0;
            race_boost_start();
            if (c->f544 != 0) {
                sound_stop(c->f544);
            }
            c->f544 = sound_at_rear(c, 3, 0, 0, 0xa000);
        } else if (c->f53c == 0 && c->f548 <= 0 && c->f534 > 20.0 && d->control != 1) {
            /* Not full yet. */
            c->f544 = sound_at_rear(c, 3, 1, 0, 0x5000);
            c->f548 = 30;
        }
        if (c->f548 > 0) {
            c->f548--;
        }
    } else if (action == 2) {
        c->f53c = 2;
        c->f548 = 0;
        race_boost_start();
        if (c->f544 != 0) {
            sound_stop(c->f544);
        }
        c->f544 = sound_at_rear(c, 3, 15, 0, 0x10000);
    }
}

/* 0x442670
 * The engine: the drive force from the gear's torque curve at the
 * car's speed, eased into the body's, and the revs. A negative throttle
 * drives in reverse; `forward` is 1 in a forward gear. A torque bonus
 * (f51c) builds while the curve is flat, and with the handicap on, the
 * player's car gets 1.5 a place behind the leader. */
void race_engine(int throttle, int forward)
{
    int i = r_5285c0, kmh, idx;
    struct car *c = &g_cars[i];
    struct race_driver *d = &g_drivers[i];
    struct car_phys *p = &g_car_phys[i];
    const s16 *torque = g_car_curves_built[d->car][c->gear];
    double force, r;

    kmh = abs((s32)(p->speed * 3.6));
    if (d->f08 == 2 && forward == 1) {
        throttle = throttle < 50 ? 0 : throttle * 2 - 50;
    }
    if (coasting(c)) {
        throttle = 0;
    }
    idx = (s32)(kmh / g_gear_scale[g_mirror]);
    if (idx > 199) {
        idx = 199;
    }
    if (c->slip > HALF_PI && c->slip < THREE_HALF_PI && c->f118 > 2.0 && throttle > 0) {
        /* Driving against the way the car slides. */
        c->skid = 10;
        idx = 0;
    }
    if (torque[idx] <= 3) {
        c->f520++;
        if (c->f51c * 8 + 15 <= c->f520) {
            c->f51c++;
            c->f520 = 0;
        }
    } else if (c->f51c > 0) {
        c->f51c /= 2;
    }
    if (r_4949c0 == 1) {
        if (d->control == 0 && g_race_mode != 2) {
            if (c->f59c + 182.0 < g_race_clock) {
                c->f594 = (c->grid - 1) * 1.5;
                c->f59c = g_race_clock;
            }
        } else {
            c->f594 = 0.0;
        }
    }
    if (0.0 < p->speed && c->gear == 0) {
        force = -25000.0;
    } else {
        force = ((double)(torque[idx] + c->f51c) + (double)c->ai_skill + c->f594) * 1000.0
            * (throttle * 0.01);
    }
    c->throttle_on = throttle > 50;
    if (r_552e48 < 0.0) {
        p->drive += (force - p->drive) * 0.2;
    }
    if (throttle == 0) {
        p->drive = -(p->speed * 500.0);
    }
    if (p->slipping == 1 && c->f270 == 0) {
        c->skid = 30;
    }
    if (d->control != 1) {
        r = (double)idx / (double)g_car_curves[(d->car * 3 + c->gear) * 8 + 4];
        if (r <= 1.5) {
            if (c->f270 != 1 && r_552e48 == -1.0) {
                c->rpm = r * 66.0;
            } else {
                c->rpm += ((double)abs(throttle) - c->rpm) * 0.16666666666666666;
            }
        }
        if (c->f298 == 1 && c->f118 < 60.0) {
            c->rpm += (100.0 - c->rpm) * 0.16666666666666666;
        }
        if (c->f528 == 1) {
            c->rpm += -c->rpm * 0.16666666666666666;
        }
    } else {
        r = (double)idx / (double)g_ai_curves[c->gear * 8 + 4];
        if (r <= 1.5) {
            if (c->f270 == 1 && throttle > 80) {
                c->rpm += ((double)throttle - c->rpm) * 0.16666666666666666;
            } else {
                c->rpm = r * 66.0;
            }
        }
        if (c->f298 == 1 && c->f118 < 60.0) {
            c->rpm += (100.0 - c->rpm) * 0.16666666666666666;
        }
    }
}

/* 0x442d70
 * The brake: on past 50, always on before the start (r_552e48 above -1)
 * and full while coasting after the finish. Hard braking at speed
 * screeches. */
void race_brake(int brake)
{
    int i = r_5285c0;
    struct car *c = &g_cars[i];
    struct car_phys *p = &g_car_phys[i];

    if (coasting(c)) {
        brake = 100;
    }
    p->brake = brake > 50;
    if (r_552e48 > -1.0) {
        p->brake = 1;
    }
    if (brake > 80 && p->speed > 2.0) {
        c->skid = 10;
    }
}

/* 0x444c20
 * The steering: the front wheels' angle for `steer`, less at speed by
 * the car's tables (the computer's are the twelfth entry) and the
 * surface's f148. The keyboard eases toward it at the car's rate (twice
 * as fast when centring); a joystick and the computer set it. */
void race_steer(int steer)
{
    int i = r_5285c0;
    struct car *c = &g_cars[i];
    struct race_driver *d = &g_drivers[i];
    struct car_phys *p = &g_car_phys[i];
    double s = steer, scale = 0.3, rate, a;
    int t = d->control == 1 ? 11 : d->car;

    c->steer_shown += (c->f0a8 * 0.01 * s * 4.0 - c->steer_shown) * 0.1;
    rate = g_steer_rate[d->car];
    if (fabs(c->f118) < g_steer_speed[t]) {
        scale = (g_steer_speed[t] - c->f118) / g_steer_range[t] + 0.3;
    }
    a = s * 0.01 * (5.0 / c->f148) * scale;
    if (a == 0.0) {
        rate *= 0.5;
    }
    if (d->control != 1 && d->f08 <= 0) {
        p->steer += (a - p->steer) / rate;
    } else {
        p->steer = a;
    }
}

/* 0x444e20
 * The automatic gearbox: first gear while the first's torque (car 0's
 * curves, for every car) beats the second's at this speed, or going
 * backwards. The change waits until the driver's f10 has counted past
 * f14 steps of wanting it. */
void race_gearbox(int car)
{
    struct car *c = &g_cars[car];
    struct race_driver *d = &g_drivers[car];
    int kmh = abs((s32)(g_car_phys[car].speed * 3.6)), want;

    if (kmh > 199) {
        kmh = 199;
    }
    if (g_car_curves_built[0][1][kmh] > g_car_curves_built[0][2][kmh]) {
        want = 1;
    } else {
        want = forwards(c->slip) ? 2 : 1;
    }
    if (c->gear != want) {
        d->f10++;
    } else {
        d->f10 = 0;
    }
    if (d->f14 < d->f10) {
        if (c->gear != want && d->control != 1) {
            sound_at_rear(c, 4, 2, 0, 0xd000);
        }
        c->gear = want;
    }
}

/* 0x442ec0
 * The boost: while on (f53c) it burns 2.5 of f534 a step into the drive
 * force, more at low speed; the standing-start form (2) also kicks the
 * body along the heading. Off, it refills at the car's rate, with a
 * sound for the players when full. f550 restarts it for a car over
 * the network. */
void race_boost(void)
{
    int i = r_5285c0;
    struct car *c = &g_cars[i];
    struct race_driver *d = &g_drivers[i];
    struct car_phys *p = &g_car_phys[i];
    float low;

    if (c->f550 == 1) {
        race_boost_start();
        if (c->f544 != 0) {
            sound_stop(c->f544);
        }
        c->f544 = sound_at_rear(c, 3, 0, 0, 0xa000);
        c->f550 = 0;
    }
    if (c->f53c != 0) {
        c->f534 -= 2.5;
        if (!(0.0 < c->f534)) {
            c->f534 = 0.0;
            c->f53c = 0;
            c->f54c = 0;
        }
        low = (float)((20.0 - p->speed) * 10000.0);
        if (low < 0.0f) {
            low = 0.0f;
        }
        if (c->f53c == 1) {
            p->drive = g_boost_force[d->control == 1 ? 11 : d->car] * c->f534 + p->drive + low;
        } else if (c->f53c == 2) {
            p->drive = c->f534 * 20000.0 + p->drive + low;
            p->vx += cos(c->heading) * c->f534 * 0.01;
            p->vz += sin(c->heading) * c->f534 * 0.01;
        }
    } else if (c->f534 < 100.0) {
        c->f534 += g_boost_refill[d->car];
    } else if (c->f534 > 100.0) {
        if (i == 0 || (g_two_players == 1 && i == 1)) {
            c->f544 = sound_at_rear(c, 3, 2, 0, 0x10000);
        }
        c->f534 = 100.0;
    }
    if (c->f53c > 0) {
        STUB(0x443640, "race_boost_flames");
    }
}

/* 0x443320
 * The boost starting: a sound by car, car 3's own loop if it is not
 * playing, and ten sparks (0x434380, not ported). */
void race_boost_start(void)
{
    int i = r_5285c0;
    struct car *c = &g_cars[i];
    struct race_driver *d = &g_drivers[i];

    sound_at_rear(c, 3, d->car + 4, 0, 0x10000);
    if (d->car == 3 && sound_voice_of(c->f5c4) == NULL) {
        c->f5c4 = race_sound_3d(0, 3, 3, 1, 0xe000, 0, 0, (float)(s32)c->x, (float)(s32)c->z);
    }
    STUB(0x434380, "race_boost_start sparks");
}
