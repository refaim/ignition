/*
 * race_camera.h - the chase camera and the car visuals of a step.
 */

#ifndef IGN_RACE_CAMERA_H
#define IGN_RACE_CAMERA_H

#include "types.h"

extern s32 r_525e44;                    /* the ghost's time, hundredths */

void race_chase_camera(void);           /* 0x43c910, car r_5285c0 */
void race_car_visuals(void);            /* 0x435350 */

#endif /* IGN_RACE_CAMERA_H */
