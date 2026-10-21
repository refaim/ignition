/*
 * race_frame_stubs.c - the race frame's parts not ported yet; each logs
 * once.
 */

#include "race.h"
#include "race_frame.h"
#include "race_hud.h"
#include "sound.h"
#include "stubs.h"

/* 0x417d20 */
void race_f_417d20(void)
{
    STUB(0x417d20, "race_f_417d20");
}

/* 0x42bc20 */
void race_f_42bc20(void)
{
    STUB(0x42bc20, "race_f_42bc20");
}

/* 0x434580 */
void race_f_434580(void)
{
    STUB(0x434580, "race_f_434580");
}

/* 0x445940 */
void race_f_445940(void)
{
    STUB(0x445940, "race_f_445940");
}

/* 0x420eb0 */
void race_f_420eb0(void)
{
    STUB(0x420eb0, "race_f_420eb0");
}


/* 0x444450 */
void race_f_444450(void)
{
    STUB(0x444450, "race_f_444450");
}

/* 0x434190 */
void race_f_434190(void)
{
    race_age_messages();
    STUB(0x434190, "race_f_434190");
}

/* 0x4357a0 */
void race_f_4357a0(void)
{
    STUB(0x4357a0, "race_f_4357a0");
}

/* 0x441390 */
void race_f_441390(void)
{
    STUB(0x441390, "race_f_441390");
}

/* 0x4383b0 */
void race_f_4383b0(void)
{
    STUB(0x4383b0, "race_f_4383b0");
}

/* 0x438880 */
void race_f_438880(void)
{
    STUB(0x438880, "race_f_438880");
}

void sound_tick_race(void)
{
    sound_tick();
}

