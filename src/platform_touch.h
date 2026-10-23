/*
 * platform_touch.h - the on-screen pad for touch screens: a d-pad on
 * the left, A, B, X and Y on the right, Select and Start at the top.
 * The hosts with a touch screen (web, Android, iOS, and Linux and
 * Windows when one is touched) pass their fingers here, add
 * touch_buttons() to the gamepad's buttons, and call touch_draw()
 * before they show a frame. The pad shows once a finger has touched
 * the screen.
 */

#ifndef IGN_PLATFORM_TOUCH_H
#define IGN_PLATFORM_TOUCH_H

#include "types.h"

/* A finger went down, moved (`down` 1) or lifted (0); `x` and `y` are
 * in pixels of the drawable, `id` tells fingers apart. */
void touch_finger(long id, float x, float y, int down);

/* The PAD_* buttons (platform_common.h) the fingers hold, on a drawable
 * of `w` x `h`. */
u32 touch_buttons(int w, int h);

/* Draws the pad over the frame on the screen (rnd.h). */
void touch_draw(int w, int h);

#endif /* IGN_PLATFORM_TOUCH_H */
