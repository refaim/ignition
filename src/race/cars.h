/*
 * race/cars.h - the race's cars.
 */

#ifndef IGN_RACE_CARS_H
#define IGN_RACE_CARS_H

#include "structs/car.h"
#include "structs/car_phys.h"
#include "types.h"

/* A car model's wheel (0x563da4, 12 bytes). */
struct wheel_info {
    s32 verts;
    s32 top;        /* |the wheel's largest z| */
    s32 dz;         /* |its z from the body| */
};

/* The box the collisions use for a car (0x603710, 0x78 bytes). */
struct car_hull {
    s32 v[30];
};

extern struct car *g_cars;                  /* 0x5daffc, one a driver */
extern struct car_phys *g_car_phys;         /* 0x563d54 */
extern struct wheel_info *g_wheel_info;     /* 0x563da4 */
extern s32 *g_car_meshes[11];               /* 0x5db000 */
extern s32 *g_wheel_meshes[44];             /* 0x552e60 */
extern s32 g_wheel_dx[44];                  /* 0x5533b0 */
extern s32 g_wheel_dz[44];                  /* 0x553300 */
extern s16 g_car_curves_built[11][3][200];  /* 0x54f9f0 */
extern s32 g_driver_wheels[11][4][0x208];   /* 0x5537d0 */
extern struct car_hull g_car_hulls[11];     /* 0x603710 */
extern s32 r_5530bc, r_5530c0, r_5530c4;
extern s32 r_552d80, r_552f5c;
extern s32 g_car7[11][0x32];             /* 0x527f80 */

double angle_wrap(double a);
void race_load_cars(void);

#endif /* IGN_RACE_CARS_H */
