/*
 * race/contact.h - a car on the ground: the surface under its corners,
 * laps, trigger zones, and what they set off.
 */

#ifndef IGN_RACE_CONTACT_H
#define IGN_RACE_CONTACT_H

#include "types.h"

/* A request for an effect (0x639330): smoke, debris, an explosion. The
 * race fills the one global and hands it to 0x434380; fields it does not
 * set keep what the last request left. Named by offset. */
struct race_effect {
    s32 type;               /* +0x00: 1 smoke, 3 explosion, 5 debris, 7 splash, 10 off the track */
    s32 x, y, z;            /* +0x04, world units times 1024 */
    s32 f10, f14, f18;      /* +0x10: the car, or a velocity times 1024 */
    s32 f1c, f20, f24, f28, f2c, f30, f34;
    union {
        s32 i;
        const s32 *frames;  /* smoke: its frames (0x497ff8) */
    } f38;                  /* +0x38 */
    s32 f3c, f40, f44, f48, f4c, f50, f54, f58, f5c, f60;
};  /* 0x64 bytes in the original */

extern struct race_effect g_effect;         /* 0x639330 */
extern s32 r_563d20, r_4949a8;              /* set when a player finishes */
extern s32 r_619300;                        /* frames until the bump sound plays again */
extern s32 g_squash_meshes[11][2000];       /* 0x603ae0, 8000 bytes a car */

/* 0x446180: a random number, 0 <= r < 1. */
float race_rand(void);

/* 0x434380: starts an effect (race/fx.c; race_effect_start returns its
 * slot). */
void race_effect(struct race_effect *e);

/* A message on the screen (0x552f24, five of them). */
struct race_msg {
    s32 active;
    char text[0x64];
    s32 life;               /* +0x68, ticks it stays */
    s32 blink;              /* +0x6c, ticks on, then as many off */
    s32 age;                /* +0x70 */
    s32 view;               /* +0x74, -1 the whole screen */
};

_Static_assert(sizeof(struct race_msg) == 0x78, "race_msg");

/* 0x441170: shows a message, at most five at once; view -1 is the
 * whole screen, 0 or 1 a player's half. Returns its slot or -1. */
int race_message(const char *text, int a, int b, int view);

void race_contact(int car);     /* 0x424570 */
void race_squash(int car);      /* 0x4269a0 */
void race_knockout(void);       /* 0x423620 */

#endif /* IGN_RACE_CONTACT_H */
