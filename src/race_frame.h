/*
 * race_frame.h - the parts of the race's frame, named by address until
 * they are ported.
 */

#ifndef IGN_RACE_FRAME_H
#define IGN_RACE_FRAME_H

#include "types.h"

void race_f_420d10(void);
void race_f_417d20(void);
void race_f_427d70(int car);
void race_f_422680(double dt);
void race_f_435910(void);
void race_f_43d540(int car);
void race_f_43c910(void);
void race_f_42bc20(void);
void race_f_434580(void);
void race_f_43edf0(int view, double dt);
void race_f_445940(void);
void race_f_435350(void);
void race_f_420eb0(void);
void race_f_420e60(void);
void race_f_442030(int accel, int brake, int steer, int action);
void race_f_444e20(int car);
void race_f_442ec0(void);
void race_f_444450(void);
void race_f_434190(void);
void race_f_4357a0(void);
void race_f_436990(double dt);
void race_f_441390(void);
void race_f_4383b0(void);
void race_f_438880(void);

double race_frame_time(void);
void race_frame(void);

/* The sound mixing step the race calls (0x457890). */
void sound_tick_race(void);

#endif /* IGN_RACE_FRAME_H */
