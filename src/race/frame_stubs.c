/*
 * race/frame_stubs.c - the race frame's parts not ported yet; each logs
 * once.
 */

#include "race/race.h"
#include "race/frame.h"
#include "race/hud.h"
#include "../sound.h"
#include "stubs.h"



void sound_tick_race(void)
{
    sound_tick();
}

