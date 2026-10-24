/*
 * race/camera.c - the chase camera (0x43c910) and the car visuals the
 * race works out each step (0x435350).
 */

#include <math.h>

#include "race/race.h"
#include "race/camera.h"
#include "race/fx.h"
#include "race/cars.h"
#include "race/init.h"
#include "stubs.h"

extern s32 g_cd_last_track;             /* game.c, 0x4949e0 */
extern double r_553090;                 /* race/race.c, the race's time in seconds */
extern double r_54f980, r_552f68;
extern s32 r_6192a0[10], r_6192c8[10];
extern s32 r_553280, r_5530a8, r_563c60, r_54f9d0, r_5db038;

/* The loaded ghost's time in hundredths of a second; the ghost loader
 * (0x420240, not ported) sets it, 3000000 without a ghost. */
s32 r_525e44;

static const double PI = 3.1415926535898;
static const double TWO_PI = 6.2831853071796;

/* The difference a - b brought into -pi .. pi, once each way. */
static double angle_diff(double a, double b)
{
    double d = a - b;

    if (d > PI) {
        d -= TWO_PI;
    }
    if (d < -PI) {
        d += TWO_PI;
    }
    return d;
}

/* Wraps a heading once into 0 .. 2 pi and gives its yaw in tenths of a
 * degree, 0 .. 3599. */
static double heading_yaw(double *h)
{
    double yaw;

    if (*h >= TWO_PI) {
        *h -= TWO_PI;
    }
    if (!(0.0 < *h)) {
        *h += TWO_PI;
    }
    yaw = *h * -572.957795130822 + 900.0;
    if (!(0.0 <= yaw)) {
        yaw += 3600.0;
    }
    if (yaw > 3599.0) {
        yaw -= 3600.0;
    }
    return yaw;
}

/* 0x43c910
 * The chase camera of car r_5285c0. While the car races (or drives on
 * autopilot after the finish) the camera's points half its distance
 * behind and ahead of the car follow it. The camera's heading eases an
 * eighth of the way to the car's each step, its height toward a height
 * that rises on the road's slopes (by the track piece's tilt) and
 * swings with the angle between camera and road; the distance pulls in
 * on those angles. Easing is slower (1/25) once the race is on than in
 * the countdown (1/10). */
void race_chase_camera(void)
{
    int i = r_5285c0;
    struct car *c = &g_cars[i];
    struct race_cam *cam = &g_race_cams[i];
    double rate = 0.0 <= r_552e48 ? 25.0 : 10.0, rate_h = rate;
    double road, swing, tilt;
    s32 seg, plc, slope;

    if (c->f528 == 0 || c->f604 == 1) {
        cam->x = (double)cam->dist * cos(cam->heading) * -0.5 + c->x;
        cam->y = c->y;
        cam->z = (double)cam->dist * sin(cam->heading) * -0.5 + c->z;
        cam->fa8 = c->heading;
        if (c->f554 == 0) {
            cam->fb0 = c->f110;
        }
        cam->back_x = (double)cam->dist * cos(cam->heading) * 0.5 + c->x;
        cam->back_y = c->y;
        cam->back_z = (double)cam->dist * sin(cam->heading) * 0.5 + c->z;
        cam->back_target = c->heading + PI;
    }
    if (c->f354 == 0 && c->f358 == 0 && c->f35c == 0) {
        cam->f30 = cam->f30 * 0.98;
    } else {
        cam->f30 = (150.0 - cam->f30) * 0.03333333333333333 + cam->f30;
    }
    cam->heading = angle_diff(cam->heading, cam->fa8) * -0.125 + cam->heading;
    cam->yaw = heading_yaw(&cam->heading);
    cam->back_heading = angle_diff(cam->back_heading, cam->back_target) * -0.125 + cam->back_heading;
    cam->back_yaw = heading_yaw(&cam->back_heading);

    /* The road's heading here; in a fork's junction, between the two
     * branches'. */
    seg = c->f364 < 0 ? -c->f364 : c->f364;
    if (c->f36c == 3) {
        road = (g_track_links[seg].heading + g_track_links[seg].alt_heading) * 0.5;
    } else if (c->f364 >= 0) {
        road = g_track_links[seg].heading;
    } else {
        road = g_track_links[seg].alt_heading;
    }
    swing = c->f528 == 0 ? (cos((cam->heading - road - 1.5707963267949) * 2.0 + PI) + 1.0) * 50.0 : 0.0;
    tilt = c->f35c == 0 ? cam->fb0 * 114.59155902616439 : 0.0;
    if (!(cam->f48 <= 3600.0) || cam->f48 < -3600.0) {
        g_cd_last_track = 0;
    }
    cam->f48 = ((swing * 3.0 + (cam->height - (double)cam->f50 - cam->y) * 0.5 + tilt) - cam->f48) / rate
        + cam->f48;

    /* The piece's slope: 1..10 down, 11..20 up, 25 a step. Above 20 the
     * original keeps the roll term in its place. */
    plc = c->f364 >= 0 ? g_track_links[c->f364].plc : g_track_links[seg].alt_plc;
    slope = g_plc_extra[plc].f1;
    if (slope <= 0) {
        tilt = 0.0;
    } else if (slope < 11) {
        tilt = (double)-(slope * 25);
    } else if (slope <= 20) {
        tilt = (double)((slope * 5 - 50) * 5);
    }
    if (cam->fc0 < tilt) {
        cam->fc0 = c->f118 * 0.3333333333333333 + cam->fc0;
    }
    if (cam->fc0 > tilt) {
        cam->fc0 = c->f118 * -0.3333333333333333 + cam->fc0;
    }
    cam->height = ((double)cam->f50 - (swing * -2.0 - cam->y + cam->fc0 + cam->height)) / rate_h + cam->height;
    if (cam->y + 200.0 > cam->height) {
        cam->height = cam->y + 200.0;
    }
    cam->dist = (s32)((double)cam->dist + ((swing * -2.0 + 400.0) - (double)cam->dist) * 0.1);
}

/* 0x435350
 * Each car's shown pitch and roll (tenths of a degree, with the body's
 * sway) ease a quarter of the way to the car's; in a crash roll they
 * follow it exactly. Then the results scroll in once the player's car
 * (both, with two players) has finished, a page at a time, and the
 * animation counters tick. */
void race_car_visuals(void)
{
    int k;

    for (r_5285c0 = 0; r_5285c0 < g_driver_count; r_5285c0++) {
        struct car *c = &g_cars[r_5285c0];

        if (c->f554 == 0) {
            double d;

            c->f584 = ((c->f108 * 57.295779513082195 + c->f0b8) * 10.0 - c->f584) * 0.25 + c->f584;
            d = fmod((c->f110 * 57.295779513082195 + c->f0c0) * 10.0 - c->f58c, 3600.0);
            while (d > 1800.0) {
                d -= 3600.0;
            }
            while (d < -1800.0) {
                d += 3600.0;
            }
            c->f58c = d * 0.25 + c->f58c;
        } else {
            c->f584 = c->f108 * 57.295779513082195 * 10.0;
            c->f58c = c->f110 * 57.295779513082195 * 10.0;
        }
        race_scrape_sparks();
        if (0.0 < c->f60c && c->f60c < 10.0) {
            c->f60c += 0.3;
        }
    }
    race_weather();
    if (((g_two_players == 0 && g_cars[0].f528 == 1)
         || (g_two_players == 1 && g_cars[0].f528 == 1 && g_cars[1].f528 == 1))
        && !(r_54f980 < 120.0) && r_563c60 == 0) {
        r_5db038 += 10;
        if (r_5db038 > 319) {
            r_5db038 = 319;
            if (g_race_mode != 3) {
                if (r_553280 - 1 > r_54f9d0) {
                    r_5db038 = 0;
                    r_54f9d0++;
                }
                if ((double)(r_525e44 / 100) < r_553090 && r_54f9d0 < 2
                    && ((g_cars[0].f528 == 0 && r_553280 == 0)
                        || (g_cars[0].f528 == 1 && r_553280 == 1))) {
                    r_5db038 = 0;
                    r_54f9d0++;
                }
            } else if (5 - r_54f9d0 < r_553280 - 1) {
                r_5db038 = 0;
                r_54f9d0--;
            }
        }
    }
    for (k = 0; k < 10; k++) {
        r_6192a0[k]++;
        r_6192c8[k]++;
    }
    if (r_5530a8 == 7 && r_552f68 < 1.57) {
        r_552f68 += 0.1;
    }
}
