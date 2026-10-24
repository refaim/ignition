/*
 * race_hud.h - the race's HUD: times, place, gear, boost, the dials,
 * hint signs, the start lights, the finishing place and the messages.
 */

#ifndef IGN_RACE_HUD_H
#define IGN_RACE_HUD_H

#include "lisa/sprite.h"
#include "types.h"

/* The race's 2D sprites, cut from the HUD pictures (0x5285e0..). */
extern struct sprite *g_hud_panels[4];      /* 0x5285e0: time, lap, place, dial */
extern struct sprite *g_hud_needle_cap;     /* 0x5285f0 */
extern struct sprite *g_hud_gears[9];       /* 0x5285f4, four a gear */
extern struct sprite *g_hud_boost[3];       /* 0x528618: frame, lamp off, lamp on */
extern struct sprite *g_hud_lights[4];      /* 0x528654: the start, 3 2 1 GO */
extern struct sprite *g_hud_signs16[16];    /* 0x528664: the hint signs */
extern struct sprite *g_race_5286f4[6];     /* 0x5286f4, from SYSGFX */
extern struct sprite *g_race_52870c[12];    /* 0x52870c, from SYSGFX */
extern struct sprite *g_hud_car_icons[12];  /* 0x528764, by car */
extern struct sprite *g_hud_knockout[10];   /* 0x52873c, by the car's f60c */
extern struct sprite *g_race_5287e8;        /* 0x5287e8 */
extern struct sprite *g_hud_bar[2];         /* 0x5287ec: the progress bar, low and high resolution */
extern struct sprite *g_hud_leader;         /* 0x5287f4 */
extern struct sprite *g_hud_places4[4];     /* 0x528794: 0, then the 1st, 2nd and 3rd place */
extern struct sprite *g_race_5287b8[3];     /* 0x5287b8, from POKAL */
extern struct sprite *g_race_5287c4[3];     /* 0x5287c4 */
extern struct sprite *g_race_5287d0[3];     /* 0x5287d0 */
extern struct sprite *g_race_5287dc[3];     /* 0x5287dc */

/* 0x41f3a0: the HUD's layout for the screen and views. */
void race_hud_layout(void);
/* 0x41f410: cuts the HUD's sprites again for the layout. */
void race_hud_recut(void);
/* 0x41e990: the layout and all the race's 2D sprites. */
void race_hud_sprites(void);

/* 0x4407c0: seconds as "MM:SS:hh", in a static buffer. */
const char *race_time_text(double seconds);

/* 0x43ef30: the HUD of the view of driver `view`; `second` is 1 for the
 * second player's half. */
void race_hud(int view, int second);
/* 0x441250 */
void race_draw_messages(void);
/* 0x441210 */
void race_age_messages(void);

void race_hud_free(void);

#endif /* IGN_RACE_HUD_H */
