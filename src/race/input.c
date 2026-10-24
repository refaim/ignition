/*
 * race/input.c - the players' controls (0x420e60, 0x421bc0).
 */

#include "input.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/control.h"
#include "race/frame.h"
#include "race/misc.h"
#include "race/init.h"

/* 0x4461d0 */
static double sign(double x)
{
    if (0.0 < x) {
        return 1.0;
    }
    if (x == 0.0) {
        return 0.0;
    }
    if (0.0 > x) {
        return -1.0;
    }
    return -2.0;
}

/* The heading of the road piece the car is on: a negative place is on
 * a fork's second branch. */
static double road_heading(s32 place)
{
    if (place >= 0) {
        return g_track_links[place].heading;
    }
    return g_track_links[-place].alt_heading;
}

static int key(const struct race_driver *d, int k)
{
    return input_key_down(d->keys[k]) == 1;
}

/* 0x421bc0: the keyboard. Keys are left, right, accelerate, brake, gear
 * up, gear down, the action and look back. */
static void race_keyboard(void)
{
    int i = r_5285c0;
    struct race_driver *d = &g_drivers[i];
    struct car *c = &g_cars[i];
    s32 accel = 0, brake = 0;

    if (d->f08 == 0) {
        if (key(d, 2)) {
            accel = 100;
        }
        if (key(d, 3)) {
            brake = 100;
        }
        if (key(d, 0)) {
            d->f24 = d->keys[0];
            c->steer_heading = road_heading(c->f364) - 1.5707963267949;
            c->steer_pull = 50;
            c->steer = -100;
        }
        if (key(d, 1)) {
            d->f24 = d->keys[1];
            c->steer_heading = road_heading(c->f364) - 1.5707963267949;
            c->steer_pull = -50;
            c->steer = 100;
        }
        if (!input_key_down(d->keys[0]) && !input_key_down(d->keys[1])) {
            double pull = c->steer_pull;

            c->steer_pull = (s32)(pull - sign(pull));
            c->steer = 0;
        }
        if (d->f18 == 1 && brake == 0) {
            accel = 100;
        }
        if (c->f558 > 0) {
            c->steer = -c->steer;
        }
        if (key(d, 6) && c->f354 == 0 && c->f35c == 0 && c->f358 == 0) {
            c->action = 1;
        } else {
            c->action = 0;
        }
        if (c->f3a4 < 0.25 && c->action == 1 && c->last_action == 0 && c->f53c == 0) {
            c->action = 2;
        }
        if (c->look_back == 0) {
            if (key(d, 7)) {
                c->look_back = 1;
            }
        } else if (c->look_back == 1 && !input_key_down(d->keys[7])) {
            c->look_back = 0;
        }
        race_control(accel, brake, c->steer, c->action);
    }
    /* The gears, a press at a time, unless they are automatic. */
    if (key(d, 4) && d->f28 != d->keys[4] && d->f0c == 0) {
        d->f28 = d->keys[4];
        if (c->gear < 2) {
            c->gear++;
        }
    }
    if (key(d, 5) && d->f28 != d->keys[5] && d->f0c == 0) {
        d->f28 = d->keys[5];
        if (c->gear > 0) {
            c->gear--;
        }
    }
    if (!input_key_down(d->keys[5]) && !input_key_down(d->keys[4])) {
        d->f28 = 0;
    }
    if (d->f0c == 1 && c->gear != 0) {
        race_gearbox(i);
    }
    c->last_action = c->action;
}

/* 0x420e60 */
void race_f_420e60(void)
{
    if (r_494990 == 0) {
        if (g_drivers[r_5285c0].f08 > 0) {
            race_joystick();
        }
        race_keyboard();
    } else {
        race_control(0, 0, 0, 0);
    }
}
