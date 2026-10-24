/*
 * menu/car.h - the front end's 3D: the turning cars of the car select
 * pages and the animated background (0x40c5a0..0x410930).
 */

#ifndef IGN_MENUCAR_H
#define IGN_MENUCAR_H

#include "types.h"

int menucar_load(void);
int menucar_free(void);
int car3d_draw(s32 car, int spin, double scale, int x, int y);

#endif /* IGN_MENUCAR_H */
