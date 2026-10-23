/*
 * race_results.h - the screens after the race.
 */

#ifndef IGN_RACE_RESULTS_H
#define IGN_RACE_RESULTS_H

#include "types.h"

extern s32 r_563d68;                    /* RETURN goes on to the next track */
extern s32 r_525e70[11];                /* the cars by total score */
extern s32 r_553040[11];                /* their totals */
extern char r_601680[16];               /* the ghost's driver */

/* 0x438a60: the results, page by page as r_5530a8 moves on; returns 0. */
int race_results(void);

#endif /* IGN_RACE_RESULTS_H */
