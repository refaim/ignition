/*
 * race_frame_stubs.c - the race frame's parts not ported yet; each logs
 * once.
 */

#include "race.h"
#include "race_frame.h"
#include "race_hud.h"
#include "sound.h"
#include "stubs.h"


/* 0x434190 */
void race_f_434190(void)
{
    extern void race_effects_tick(void);
    race_effects_tick();
}

void sound_tick_race(void)
{
    sound_tick();
}

