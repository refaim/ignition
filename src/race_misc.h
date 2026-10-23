/*
 * race_misc.h - the race frame's smaller steps.
 */

#ifndef IGN_RACE_MISC_H
#define IGN_RACE_MISC_H

#include "types.h"

struct scene_desc;
extern struct scene_desc *g_ghost_objs;     /* 0x525e40 */
extern s32 g_ghost_car;                     /* 0x563d30 */
extern char g_ghost_driver[0x20];           /* 0x601680 */
extern s32 r_563d68;                        /* race_results.c: RETURN goes on */

void race_ground_effects(void);     /* 0x42bc20 */
void race_net_messages(void);       /* 0x417d20 */
void race_animate_objects(void);    /* 0x4357a0 */
void race_weather_zone(void);       /* 0x434580 */
void race_load_ghost(void);         /* 0x420240 */
void race_play_ghost(void);         /* 0x445940 */
void race_iris(int step);           /* 0x438210 */
void race_save_results(void);       /* 0x4222b0 */
void race_pause_menu(void);         /* 0x4383b0 */
void race_ask_box(void);            /* 0x438880 */
void race_hot_keys(void);           /* 0x420eb0 */
void race_joystick(void);           /* 0x441390 */

#endif /* IGN_RACE_MISC_H */
