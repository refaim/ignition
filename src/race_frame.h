/*
 * race_frame.h - the parts of the race's frame, named by address until
 * they are ported.
 */

#ifndef IGN_RACE_FRAME_H
#define IGN_RACE_FRAME_H

#include "types.h"

void race_f_420e60(void);

double race_frame_time(void);
void race_frame(void);

/* The sound mixing step the race calls (0x457890). */
void sound_tick_race(void);

#endif /* IGN_RACE_FRAME_H */
