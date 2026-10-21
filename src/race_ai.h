/*
 * race_ai.h - the computer's drivers: the racing line they follow
 * (from the track's .TRI) and their driving.
 */

#ifndef IGN_RACE_AI_H
#define IGN_RACE_AI_H

#include <stddef.h>

#include "types.h"

#pragma pack(push, 1)

/* A node of the racing line (0x4b bytes): the road's edge across a
 * piece, from p1 to p2, in world units (x and z plus 0x6400). A piece
 * whose .TRI type is 1 or 2 is split across into two lanes. */
struct ai_node {
    u8 marker;              /* +0x00: 0 a node; 1 the -2 piece of a fork */
    u8 surface;             /* +0x01: the .TRI's surface, into g_surface_grip */
    s32 x1, y1, z1;         /* +0x02 */
    s32 x2, y2, z2;         /* +0x0e */
    s32 ax, az;             /* +0x1a: 18 in from p1 along the edge */
    s32 bx, bz;             /* +0x22: 18 in from p2 */
    float h1;               /* +0x2a: the heading from p1 to the next node's p1 */
    float hm;               /* +0x2e: the same between the middles */
    float h2;               /* +0x32: between the p2s */
    float v1, vm, v2;       /* +0x36: the same with y for z */
    float across;           /* +0x42: the heading from p1 to p2 */
    float back;             /* +0x46: from p2 to p1 */
    u8 type;                /* +0x4a: the .TRI's type, 1 or 2 split */
};

_Static_assert(offsetof(struct ai_node, x1) == 0x02, "ai_node.x1");
_Static_assert(offsetof(struct ai_node, x2) == 0x0e, "ai_node.x2");
_Static_assert(offsetof(struct ai_node, ax) == 0x1a, "ai_node.ax");
_Static_assert(offsetof(struct ai_node, h1) == 0x2a, "ai_node.h1");
_Static_assert(offsetof(struct ai_node, across) == 0x42, "ai_node.across");
_Static_assert(offsetof(struct ai_node, type) == 0x4a, "ai_node.type");
_Static_assert(sizeof(struct ai_node) == 0x4b, "ai_node");

/* A computer driver's state (0x63a018, 0x7c bytes, one a driver). The
 * first nine come from g_ai_params by car; packed, as the doubles at
 * 0x6c and 0x74 are not aligned. */
struct ai_state {
    double skill;           /* +0x00, scaled by difficulty */
    double f08;
    double f10;             /* +0x10: slows it the nearer the corner */
    double f18;             /* +0x18: its top speed, 120 + 60 times */
    double f20;             /* +0x20: boldness: forks, the boost */
    double look;            /* +0x28: how far it watches cars, times 800 */
    double f30;
    double f38;             /* +0x38: how much bends slow it */
    double f40;             /* +0x40: how much hills slow it */
    s32 f48;
    s32 f4c, f50;
    s32 throttle;           /* +0x54: the last throttle */
    double side;            /* +0x58: where across the road, >= 1 or <= -1 */
    s32 stuck;              /* +0x60: steps it has been slow or turned */
    s16 lane;               /* +0x64: the lane it takes at a fork */
    s16 fork_lane;          /* +0x66 */
    s32 seg;                /* +0x68: the segment it was on */
    double chance;          /* +0x6c: 0..1, drawn at each fork */
    double steer;           /* +0x74 */
};

#pragma pack(pop)

_Static_assert(offsetof(struct ai_state, f48) == 0x48, "ai_state.f48");
_Static_assert(offsetof(struct ai_state, throttle) == 0x54, "ai_state.throttle");
_Static_assert(offsetof(struct ai_state, side) == 0x58, "ai_state.side");
_Static_assert(offsetof(struct ai_state, stuck) == 0x60, "ai_state.stuck");
_Static_assert(offsetof(struct ai_state, lane) == 0x64, "ai_state.lane");
_Static_assert(offsetof(struct ai_state, seg) == 0x68, "ai_state.seg");
_Static_assert(offsetof(struct ai_state, chance) == 0x6c, "ai_state.chance");
_Static_assert(offsetof(struct ai_state, steer) == 0x74, "ai_state.steer");
_Static_assert(sizeof(struct ai_state) == 0x7c, "ai_state");

extern struct ai_state *g_ai;           /* 0x63a018 */
extern struct ai_node ***g_ai_lanes;    /* 0x63a024: by segment, the lanes, 0 ended */

/* 0x4160b0's answer: the segment, lane and piece it stopped at. */
extern s16 g_ai_step_seg, g_ai_step_lane, g_ai_step_piece;

void race_load_ai_track(void);          /* 0x414e40 */
int race_free_ai_track(void);           /* 0x415e90 */
void race_ai(int car);                  /* 0x4134e0 */
void race_ai_step(s16 seg, s16 lane, s16 step);  /* 0x4160b0 */

/* The C library's rand as the original links it (0x4694d0 seeds it). */
void race_srand(u32 seed);
int race_msrand(void);

#endif /* IGN_RACE_AI_H */
