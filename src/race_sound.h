/*
 * race_sound.h - the race's sounds.
 */

#ifndef IGN_RACE_SOUND_H
#define IGN_RACE_SOUND_H

#include "types.h"

struct scene_desc;
extern s32 g_ghost_voice;                   /* 0x603ad8 */
extern struct scene_desc *g_ghost_objs;     /* 0x525e40, the ghost's body then wheels */

void race_load_sounds(void);    /* 0x41f9b0 */
void race_engine_sound(void);   /* 0x4452c0, car r_5285c0 */
void race_tyre_sound(void);     /* 0x444fa0, car r_5285c0 */
void race_exhaust(void);        /* 0x444450, car r_5285c0 */

#endif /* IGN_RACE_SOUND_H */
