/*
 * race.h - the race: loading a track, the race's state, its frame.
 *
 * The race reads the settings file again into its own copy and fans it
 * out into its globals (0x419260). Globals whose role is not known yet
 * are named r_<address>.
 */

#ifndef IGN_RACE_H
#define IGN_RACE_H

#include <string.h>

#include "types.h"

enum {
    RACE_DRIVERS = 11,
    RACE_TEX_SLOTS = 9
};

/* A driver, 0x4c bytes (0x552ffc). */
struct race_driver {
    s32 car;                /* +0x00 */
    s32 control;            /* +0x04: 1 the computer */
    s32 f08;                /* +0x08 */
    s32 f0c;                /* +0x0c */
    s32 f10;                /* +0x10 */
    s32 f14;                /* +0x14, 30 */
    s32 f18;                /* +0x18 */
    u8 keys[8];             /* +0x1c, scan codes */
    u8 f24[0x18];           /* +0x24 */
    char name[0x10];        /* +0x3c */
};

_Static_assert(sizeof(struct race_driver) == 0x4c, "race_driver");

/* The settings as the race read them (0x6393a0). */
extern u8 g_race_cfg[0x5a0];

static inline s32 race_cfg(int offset)
{
    s32 v;

    memcpy(&v, g_race_cfg + offset, sizeof(v));
    return v;
}

extern int g_race_pool;                 /* 0x55307c */
extern s32 g_race_bytes;                /* 0x494980, loaded so far */
extern u8 *g_race_buffer;               /* 0x563d40, 0x77240 bytes */
extern struct race_driver *g_drivers;   /* 0x552ffc */
extern s32 g_driver_count;              /* 0x552fe0 (Count) */
extern s32 g_race_mode;                 /* 0x527f6c: 0 championship .. 3 */
extern s32 g_track;                     /* 0x552fc4, 0..6 */
extern s32 g_two_players;               /* 0x55306c */
extern s32 g_mirror;                    /* 0x563ce8: 3 and up mirror the track */
extern s32 g_views;                     /* 0x54f98c */
extern s32 g_race_cd_track;             /* 0x552e54 */
extern s32 g_race_grid[RACE_DRIVERS];   /* 0x4972b0 */
extern char g_track_dir[16];            /* 0x553790, "CANADA\\" */
extern char g_track_name[16];           /* 0x552f40, "CANADA" */
extern char g_ghost_name[16];           /* 0x5532a0, "MOOSEJAW" */

/* The level's files. */
extern u8 *g_level_col;                 /* 0x54f900 */
extern u8 *g_level_pan;                 /* 0x5dfe64 */
extern u8 *g_level_pic;                 /* 0x525e94 */
extern u8 *g_level_shd;                 /* 0x527f74 */
extern u8 *g_level_tab;                 /* 0x563be4 */
extern s32 *g_level_pos;                /* 0x553064 */
extern s32 *g_level_plc;                /* 0x54f9cc */
extern s32 *g_cars_plc;                 /* 0x552fc8 */
extern s32 *g_level_msh;                /* 0x525e60 */
extern s32 *g_cars_msh;                 /* 0x527f34 */
/* Track, CARS, LIGHT, SMOKE, DARKSMOK, BOOM, EXSMOKE, DIVSPR, a blank
 * page (0x5530f0..0x553110), and their sizes in pages. */
extern u8 *g_race_tex[RACE_TEX_SLOTS];
extern int g_race_tex_pages[RACE_TEX_SLOTS];

/* The HUD's fonts and pictures. */
extern u8 *g_race_fnt;                  /* 0x525e58, IGNITION.FNT */
extern int g_race_fonts[9];             /* 0x528624.. */
extern u8 *g_race_pics[8];              /* s_pangfx .. pokal */

void race_load_1(void);
void race_load_2(void);
void race_load_world(void);
void race_progress(int percent);

#endif /* IGN_RACE_H */
