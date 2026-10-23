/*
 * race_fx.h - the race's effects.
 */

#ifndef IGN_RACE_FX_H
#define IGN_RACE_FX_H

#include "race_contact.h"

enum {
    FX_SLOTS = 200,
    FX_FRAMES_BASE = 0x497ef8   /* g_fx_frames[0] in the original */
};

extern struct race_effect g_effects[FX_SLOTS];  /* 0x5db040 */

/* 0x434380: the request into a slot; returns the slot or -1. */
int race_effect_start(struct race_effect *e);
/* 0x434190: the effects' step, once a tick. */
void race_effects_tick(void);
/* 0x432f20: sparks where car 0x5285c0 scrapes a wall. */
void race_scrape_sparks(void);
/* 0x434840: the rain and snow. */
void race_weather(void);
const u8 *race_palette_tint(const u8 *pal, int r, int g, int b, double f, const s32 *mask);

#endif /* IGN_RACE_FX_H */
