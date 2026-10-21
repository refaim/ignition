/*
 * race_frame_stubs.c - the race frame's parts not ported yet; each logs
 * once.
 */

#include "race.h"
#include "race_frame.h"
#include "sound.h"
#include "stubs.h"

/* 0x420d10 */
void race_f_420d10(void)
{
    STUB(0x420d10, "race_f_420d10");
}

/* 0x417d20 */
void race_f_417d20(void)
{
    STUB(0x417d20, "race_f_417d20");
}

/* 0x427d70 */
void race_f_427d70(int car)
{
    (void)car;
    STUB(0x427d70, "race_f_427d70");
}

/* 0x422680 */
void race_f_422680(double dt)
{
    (void)dt;
    STUB(0x422680, "race_f_422680");
}

/* 0x435910 */
void race_f_435910(void)
{
    STUB(0x435910, "race_f_435910");
}

/* 0x43d540 */
void race_f_43d540(int car)
{
    (void)car;
    STUB(0x43d540, "race_f_43d540");
}

/* 0x43c910 */
void race_f_43c910(void)
{
    STUB(0x43c910, "race_f_43c910");
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

/* 0x43edf0 */
void race_f_43edf0(int view, double dt)
{
    (void)view;
    (void)dt;
    STUB(0x43edf0, "race_f_43edf0");
}

/* 0x445940 */
void race_f_445940(void)
{
    STUB(0x445940, "race_f_445940");
}

/* 0x435350 */
void race_f_435350(void)
{
    STUB(0x435350, "race_f_435350");
}

/* 0x420eb0 */
void race_f_420eb0(void)
{
    STUB(0x420eb0, "race_f_420eb0");
}

/* 0x420e60 */
void race_f_420e60(void)
{
    STUB(0x420e60, "race_f_420e60");
}

/* 0x442ec0 */
void race_f_442ec0(void)
{
    STUB(0x442ec0, "race_f_442ec0");
}

/* 0x444450 */
void race_f_444450(void)
{
    STUB(0x444450, "race_f_444450");
}

/* 0x434190 */
void race_f_434190(void)
{
    STUB(0x434190, "race_f_434190");
}

/* 0x4357a0 */
void race_f_4357a0(void)
{
    STUB(0x4357a0, "race_f_4357a0");
}

/* 0x436990 */
void race_f_436990(double dt)
{
    (void)dt;
    STUB(0x436990, "race_f_436990");
    /* Until the views are ported: the stand-in view. */
    race_debug_view();
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
