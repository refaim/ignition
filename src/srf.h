/*
 * srf.h - what lies under a point of the track: the .SRF ground grid,
 * the scene's objects, trigger zones and the walls near a car.
 */

#ifndef IGN_SRF_H
#define IGN_SRF_H

#include <stddef.h>

#include "types.h"

/* A surface under a point, 0x40 bytes (0x4c53e0, 0x4c5498, 0x4c57d0).
 * For a trigger zone `normal` holds the object's position and `v` the
 * triangle in the object's mesh. */
struct srf_hit {
    s32 type;           /* +0x00, the face's surface; -1 none */
    s32 normal[3];      /* +0x04, not unit length */
    s32 v[3][3];        /* +0x10, the triangle; y is negated */
    s32 obj;            /* +0x34, the object; its index here, its address in the original */
    s32 f20;            /* +0x38, the object's f20 */
    s32 klass;          /* +0x3c, the object's class */
};

_Static_assert(offsetof(struct srf_hit, normal) == 0x04, "srf_hit");
_Static_assert(offsetof(struct srf_hit, v) == 0x10, "srf_hit");
_Static_assert(offsetof(struct srf_hit, obj) == 0x34, "srf_hit");
_Static_assert(offsetof(struct srf_hit, f20) == 0x38, "srf_hit");
_Static_assert(offsetof(struct srf_hit, klass) == 0x3c, "srf_hit");
_Static_assert(sizeof(struct srf_hit) == 0x40, "srf_hit");

/* 0x412fc0: the ground at (x, z) nearest height y, from the .SRF. */
struct srf_hit *srf_lookup(s32 x, s32 y, s32 z);
/* 0x427dc0: the ground or trigger under (x, z) among the visible
 * objects, for the car r_5285c0. */
struct srf_hit *race_obj_query(s32 x, s32 y, s32 z, s32 slot, s32 y_ofs);
/* 0x428730: the trigger zone (classes 300..349) under (x, z) near car
 * `car`; gathers the objects under the car first. */
struct srf_hit *race_zone_query(s32 x, s32 y, s32 z, s32 car);
/* 0x428b90: the edges of the ground near (x, y, z) that no other face
 * shares, as x1, z1, x2, z2 to `out`; returns how many. */
s32 race_wall_edges(double *out, s32 x, s32 y, s32 z, s32 deck);

#endif /* IGN_SRF_H */
