/*
 * race/control.h - a car's controls: throttle, brake, steering, gears
 * and boost (0x442030..0x4452c0). All but race_gearbox work on the car
 * r_5285c0 names.
 */

#ifndef IGN_RACE_CONTROL_H
#define IGN_RACE_CONTROL_H

#include "types.h"

/* What race_control takes for `accel` to apply the inputs it last
 * stored for the car again. */
enum { RACE_CONTROL_REPEAT = 0x1f02 };

void race_control(int accel, int brake, int steer, int action);
void race_engine(int throttle, int forward);
void race_brake(int brake);
void race_steer(int steer);
void race_gearbox(int car);
void race_boost(void);
void race_boost_start(void);

extern s32 r_563d58, r_563d5c;          /* the last accel and brake passed */
extern s32 r_4949c0;                    /* 1: the handicap is on */

#endif /* IGN_RACE_CONTROL_H */
