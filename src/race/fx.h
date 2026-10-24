/*
 * race/fx.h - the race's effects.
 */

#ifndef IGN_RACE_FX_H
#define IGN_RACE_FX_H

#include "race/contact.h"

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
/* The ground effects under car 0x5285c0 (0x42bc20's): 0x433f90,
 * 0x4338d0, 0x4333e0. */
void race_skids(void);
void race_dust_trail(void);
void race_landing_dust(void);
void race_spray(void);
/* The boost on car 0x5285c0: 0x443320's puffs, and 0x443640. */
void race_boost_puffs(void);
void race_boost_flames(void);
/* 0x434840: the rain and snow. */
void race_weather(void);
const u8 *race_palette_tint(const u8 *pal, int r, int g, int b, double f, const s32 *mask);

#endif /* IGN_RACE_FX_H */
