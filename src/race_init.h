/*
 * race_init.h - the last stage of loading a race.
 */

#ifndef IGN_RACE_INIT_H
#define IGN_RACE_INIT_H

#include "structs/race_cam.h"
#include "types.h"

/* The collision grid (.SRF): 0x24 bytes of header, cells, spans of 24
 * bytes and two lists of span offsets. */
struct srf_cell {
    u32 list_b;         /* byte offset into list_b */
    u32 list_a;         /* byte offset into list_a */
    u16 count_a, count_b;
};

struct srf {
    u8 *file;           /* 0x4937bc */
    s32 ext_z, ext_x;   /* 0x4c53b4, 0x4c53d0 */
    s32 cell_h, cell_w; /* 0x4c53c8, 0x4c53b8 */
    s32 gw, gh;         /* 0x4c53cc, 0x4c53b0 */
    const struct srf_cell *cells;   /* 0x4c53d4 */
    const u8 *spans;    /* 0x4c53a8 */
    const u32 *list_a;  /* 0x4c53a4, byte offsets into spans */
    const u32 *list_b;  /* 0x4c53c0 */
};

extern struct srf g_srf;
extern struct race_cam *g_race_cams;
extern s32 g_weather;
extern double g_best_lap;

/* Globals the stages set, named by address until their use is known. */
extern s32 r_5532b8, r_619308, r_5532b0;
extern double r_553288, r_5532c0, r_6192f8, r_619318;
extern s32 r_563d28, r_563d2c, r_563d38, r_563d3c, r_552f30, r_552f34;
extern double r_525e30;
extern s32 r_525e38, r_525e3c;
extern s32 r_525e48;

void race_pos_deltas(void);
int race_srf_load(const char *name);
void race_init_cameras(void);
void race_make_effects(void);
void race_clear_laps(void);
void race_reset_state(void);

#endif /* IGN_RACE_INIT_H */
