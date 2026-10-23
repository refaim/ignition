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
    s32 f24, f28;           /* +0x24 */
    s32 view_x0, view_y0;   /* +0x2c, the view on the screen */
    s32 view_x1, view_y1;   /* +0x34, exclusive */
    char name[0x10];        /* +0x3c */
};

_Static_assert(sizeof(struct race_driver) == 0x4c, "race_driver");

/* A placement of the .PLC: s32 count, then these. */
struct plc_entry {
    s32 mesh;               /* +0x00, dwords into the .MSH */
    s32 type;               /* +0x04, low 12 bits after 0x41b360 */
    s32 x, y, z;            /* +0x08 */
};

static inline struct plc_entry *plc_at(s32 *plc, int i)
{
    return (struct plc_entry *)(plc + 1) + i;
}

/* The fields packed in a placement's type (0x552e40). */
struct plc_extra {
    s32 f0, f1, f2;         /* bits 12..15, 16..23, 24..31 */
};

/* A road piece's place in the track's order and its heading
 * (0x553784, 12 bytes). Pieces on the second branch of a fork have a
 * negative order. */
struct road_order {
    s32 order;              /* 10000 for none */
    double heading;         /* radians, 0..2 pi */
};

/* The track's pieces in order (0x552f60, 24 bytes); at a fork the
 * second branch is in alt. */
struct track_link {
    s32 plc;                /* +0x00 */
    double heading;         /* +0x04 */
    s32 alt_plc;            /* +0x0c, -1 none, -2 on a fork */
    double alt_heading;     /* +0x10 */
};

extern struct plc_extra *g_plc_extra;       /* 0x552e40 */
extern struct road_order *g_road_order;     /* 0x553784, 1500 */
extern struct track_link *g_track_links;    /* 0x552f60, 1500 */
extern s32 g_track_link_count;              /* 0x5287f8 */
extern s32 g_road_count;                    /* 0x553074 */
extern u8 *g_colour_key;                    /* 0x563cf8, [s << 8 | d] = s ? s : d */

void race_unpack_types(void);
void race_make_tables(void);
void race_track_order(void);

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

/* From the settings (0x419260), named by address until their use is
 * known. */
extern s32 r_527f28, r_563c6c, r_552f10, r_553068, r_527f78, r_525e68;
extern s32 r_552e58, r_563be0, r_553084, r_54f958, r_527f20, r_5daff8;
extern s32 g_race_sfx_volume;           /* 0x601668 */
extern s32 g_race_cd_volume;            /* 0x527f30 */

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

struct scene_desc;
extern struct scene_desc *g_world;      /* 0x54f908, one a placement */

/* A sound a placement makes (0x553078). */
struct sound_src {
    s32 plc;
    s32 voice;
};

/* A lens flare's placement (0x563c68, 12 bytes). */
struct flare {
    s32 plc;
    s32 kind;               /* the type - 350 */
    s32 f08;
};

/* A moving placement (0x525e6c, 16 bytes). */
struct mover {
    s32 plc;
    s32 type;
    s32 phase;
    s32 f0c;
};

struct coll_obj;
extern struct coll_obj *g_coll_objs;            /* 0x54f904 */
extern struct sound_src *g_sound_srcs;          /* 0x553078 */
extern struct flare *g_flares;                  /* 0x563c68 */
extern struct scene_desc *g_flare_objs;         /* 0x553460 */
extern struct mover *g_movers;                  /* 0x525e6c */
extern struct scene_desc *g_car_objs;           /* 0x5db02c */
extern struct scene_desc *g_wheel_objs;         /* 0x552fbc */
extern struct scene_desc *g_car7_objs;          /* 0x552f70 */
extern struct scene_desc *g_shadow_objs;        /* 0x552f20 */
extern struct scene_desc *g_arrow_objs;         /* 0x553788 */
extern s32 r_54f954, r_563cf0, r_563d78, r_552f78, r_54f9b8, r_54f930, r_563d98;

/* 0x43e710 (race_sound.c): sample `sample` of bank a, group b at a
 * place, louder nearer the camera's car. Returns the voice's handle. */
s32 race_sound_3d(int bank, int group, int sample, int loops, int volume, int rate, int pan,
                  float x, float z);

void race_make_world(void);
void race_place_car(int car);
void race_debug_view(void);
struct scene;
struct scene_obj;
void race_view_fill(double y0, double y1, int index);
void race_draw_view(const struct scene *sc, struct scene_obj *const *objs, int n);

void race_load_1(void);
void race_load_2(void);
void race_load_world(void);
void race_progress(int percent);

#endif /* IGN_RACE_H */
