/*
 * race.c - the race's frame (inside game_frame, 0x4173fb..0x417c8c).
 *
 * Time runs in ticks of 1/36 s. The cars move once at least a tick has
 * passed, by however many ticks it was; the computer's drivers, the
 * players' controls and the cameras step once for each whole tick, and
 * the moving objects follow their paths at the same rate. Then the
 * views are drawn.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "cd.h"
#include "game.h"
#include "input.h"
#include "lisa/font.h"
#include "race.h"
#include "race_camera.h"
#include "race_cars.h"
#include "race_control.h"
#include "race_frame.h"
#include "race_fx.h"
#include "race_misc.h"
#include "race_init.h"
#include "race_phys.h"
#include "race_respawn.h"
#include "race_sound.h"
#include "race_view.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

double r_563d80;                /* this frame's ticks */
double r_54f9a0_acc;            /* ticks not yet stepped (0x54f9a0) */
double r_563bd0_acc;            /* the paths' ticks not yet stepped */
double r_563d48_last;
double r_553090;                /* the race's time in seconds */
s32 r_494984;                   /* this frame's rate */
s32 r_5285c0;                   /* the car being updated */
s32 r_4949a0;                   /* when the CD was last checked */
s32 r_5532ac_t, r_563ce4_t;     /* countdowns to the race's end */
static double s_last_tick;      /* 0x603ad0 */

/* 0x420c00: the ticks since the last call; under a tick, nothing is
 * taken and the fraction comes back. */
double race_frame_time(void)
{
    double now = (s32)g_frame_time * 0.036;
    double dt = now - s_last_tick;

    if (dt < 1.0) {
        return dt;
    }
    s_last_tick = now;
    r_494984 = (s32)(36.0 / dt);
    g_fps_sum += r_494984;
    g_fps_count++;
    if (r_494990 == 1 && r_552f10 == 2) {
        r_525e48 = (s32)(r_525e48 + dt * 27.77777777777778);
    }
    r_553090 = (s32)(g_frame_time - r_525e48) * 0.001;
    if (dt == 0.0) {
        return 0.01;
    }
    return dt > 10.799999999999999 ? 10.8 : dt;
}

/* 0x4410c0: the race's clock into each car still racing: its total time
 * and the time of the lap it is on. */
static void race_lap_clocks(void)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        struct car *c = &g_cars[i];

        if (c->f528 == 0 && (g_race_mode == 3 || c->laps < 3)) {
            c->f3a4 = r_553090;
            c->lap_time = r_553090 - c->lap_start;
        }
    }
}

/* 0x420d10
 * The start: r_552e48 counts the ticks since the race was shown, and
 * at 200 (or once the network says go, r_5532c8 1) the race's clock
 * starts from 0 and r_552e48 becomes -1, which lets the engines drive.
 * Also keeps the lap clocks, and splits the screen again once the
 * first player has finished a two player race. */
static void race_countdown(void)
{
    if (r_552e48 > -1.0) {
        r_552e48 = (s32)(g_frame_time - r_525e48) * 0.036;
        if (!(r_552e48 < 200.0) || r_5532c8 == 1) {
            if (r_552e48 < 200.0) {
                r_525e48 = (s32)-(5555.555555555556 - (double)g_frame_time);
            } else {
                race_frame_time();
                race_clear_laps();
            }
            r_552e48 = -1.0;
            r_5532c8 = 2;
            g_cars[0].net_event = 10;
        }
    }
    r_5530e0++;
    race_lap_clocks();
    if (g_two_players == 1 && g_cars[0].f528 == 1 && g_cars[1].f528 == 1 && r_552f58 == 0
        && !(r_54f980 < 120.0)) {
        r_552f58 = 1;
        race_init_hud();
    }
    if (r_563da0 > 0) {
        r_563da0 = (s32)(r_563da0 + r_563d80);
        if (r_563da0 > 20) {
            r_563c60 = 1;
        }
    }
}

/* Steps the race when a tick or more has passed. */
static void race_step(double dt)
{
    int i, finished;

    g_race_clock += dt;
    r_563d80 = dt;
    race_countdown();
    /* The countdowns after the last car finishes or the player
     * gives up. */
    if (r_5532ac_t > 0) {
        r_5532ac_t = (s32)(r_5532ac_t + r_563d80);
        if (r_5532ac_t > 30) {
            r_563d88 = 1;
            g_race_again = 0;
        }
    }
    if (r_563ce4_t > 0) {
        r_563ce4_t = (s32)(r_563ce4_t + r_563d80);
        if (r_563ce4_t > 30) {
            r_563d88 = 1;
            g_race_again = 1;
        }
    }
    if (r_552f10 != 2) {
        if (r_54f97c) {
            race_net_messages();
        }
        g_cars[0].clock = g_race_clock;
        if (r_552f10 == 3) {
            for (i = 0; i < g_driver_count; i++) {
                if (g_drivers[i].control == 1) {
                    g_cars[i].clock = g_race_clock;
                }
            }
        }
        /* Network play: 0x40e0a0 and 0x40d7a0 exchange the cars. */
        if (r_54f97c) {
            race_net_messages();
        }
    }
    for (finished = 0, i = 0; i < g_driver_count; i++) {
        if (g_cars[i].f528 == 1) {
            finished++;
        }
        if (g_cars[i].net_event == 10 && !r_5532c8) {
            r_5532c8 = 1;
        }
    }
    r_553070 = 0;
    r_553280 = finished;
    for (i = 0; i < g_driver_count; i++) {
        race_car_center(i);
    }
    race_physics(r_563d80);
    for (r_5285c0 = 0; r_5285c0 < g_driver_count; r_5285c0++) {
        struct car *c = &g_cars[r_5285c0];

        if (c->y > 1800.0) {
            race_respawn();
        }
        if (c->f354 == 0 && c->f358 == 0) {
            race_place_car(r_5285c0);
        } else {
            c->f118 = 0;
            if (g_drivers[r_5285c0].control == 2 && r_5285c0 != 0
                && g_arrow_objs[r_5285c0].in_grid == 1) {
                scene_obj_unlink(&g_arrow_objs[r_5285c0]);
            }
        }
        race_chase_camera();
        race_ground_effects();
        if (r_5daff8 == 1) {
            sound_tick_race();
        }
        if (r_5285c0 == 0 || (r_5285c0 == 1 && g_two_players == 1)) {
            race_weather_zone();
            race_wrong_way(r_5285c0, dt);
        }
    }
    if (g_race_mode == 2 && r_552e48 == -1.0 && r_553068 == 1 && r_5530b8 == 0) {
        race_play_ghost();
    }
    present_back_buffer();

    /* The fixed steps, one a tick. */
    for (r_54f9a0_acc += dt; r_54f9a0_acc >= 1.0; r_54f9a0_acc -= 1.0) {
        race_car_visuals();
        if (!r_563da0 && !r_5daff0) {
            race_hot_keys();
        }
        for (r_5285c0 = 0; r_5285c0 < g_driver_count; r_5285c0++) {
            const struct car *c = &g_cars[r_5285c0];
            s32 control = g_drivers[r_5285c0].control;

            if (c->f604 == 0 && (control == 0 || (control == 2 && r_5285c0 == 0))) {
                race_f_420e60();
            }
            if (control == 2 && r_5285c0 != 0) {
                race_boost();
                if (g_drivers[r_5285c0].car == 8) {
                    race_exhaust();
                }
            }
            if (c->f350 == 1) {
                race_respawn();
            }
        }
        race_effects_tick();
    }
    r_563bd0_acc += (s32)(g_frame_time - r_525e48) * 0.036 - r_563d48_last;
    for (; r_563bd0_acc >= 1.0; r_563bd0_acc -= 1.0) {
        race_animate_objects();
    }
    r_563d48_last = (s32)(g_frame_time - r_525e48) * 0.036;
    race_view(dt);
}

/* 0x4173fb */
void race_frame(void)
{
    double dt;

    video_restore();
    dt = race_frame_time();
    input_poll();
    if (!(r_494990 && r_552f10 == 2) && dt >= 1.0 && r_552f18 == 4) {
        race_step(dt);
    } else if (!r_563da0 && !r_5daff0) {
        race_hot_keys();
        if (r_494990 == 1 && g_drivers[0].f08 > 0) {
            race_joystick();
        }
    }
    /* Every 5 s, the CD music goes on again if it stopped. */
    if (r_4949a0 + 5 < (s32)r_553090 && race_cfg(0x0fb) != 0) {
        r_4949a0 = (s32)r_553090;
        if (!cd_is_playing()) {
            cd_play_track(g_race_cd_track);
        }
    }
    /* r_552f18 2 and 3 wait for network players; there are none. */
    if (r_494990 == 1) {
        if (r_552f10 == 2) {
            /* Off the network the race stands still: the view, as it
             * is, under the menu. */
            race_view(0);
            if (r_5532b0 != 1) {
                if (r_601674 == 0) {
                    race_pause_menu();
                } else {
                    race_ask_box();
                }
            }
            present_back_buffer();
            if (r_5daff0 > 0) {
                r_5daff0 = (s32)(r_5daff0 + dt);
                if (r_5daff0 > 20) {
                    r_563d88 = 1;
                }
            }
        } else if (r_5532b0 != 1) {
            if (r_601674 == 0) {
                race_pause_menu();
            } else {
                race_ask_box();
            }
            present_back_buffer();
        }
    }
    if (r_601674 == 1) {
        race_ask_box();
    }
    if (r_563d88) {
        g_race_load_stage = 0;
        g_race_running = 0;
        g_race_ending = 1;
    }
}
