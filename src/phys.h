/*
 * phys.h - a car's rigid body.
 */

#ifndef IGN_PHYS_H
#define IGN_PHYS_H

#include "structs/car_phys.h"

void phys_step(struct car_phys *p, int airborne, double slope);
void phys_corners(struct car_phys *p, double scale);
int phys_impulse(struct car_phys *a, struct car_phys *b, double cx, double cz, double normal);
int phys_collide(struct car_phys *a, struct car_phys *b);
int phys_wall_impulse(struct car_phys *p, double cx, double cz, double normal);
int phys_walls(struct car_phys *p, const double *segs, int count);

#endif /* IGN_PHYS_H */
