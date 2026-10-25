/*
 * menu/pages.h - what the front end draws for its pages.
 */

#ifndef IGN_MENU_PAGES_H
#define IGN_MENU_PAGES_H

#include "lisa/sprite.h"
#include "types.h"

extern s32 g_car_order[11];
extern double g_menu_background_t;
extern s32 g_car_count;
extern struct sprite *g_sprite_track;
extern struct sprite *g_sprite_flags[5];

int menu_redraw_page(void);
/* The port's: what a tap at (lx, ly), logical, asks of a page whose
 * choices are pictures (the flags), as menu_touch; -1 for other pages. */
int menu_pages_touch(float lx, float ly, int *count);
int menu_best_times(void);
const char *format_time(double t);
void draw_sprite_at(const struct sprite *s, int x, int y);
void draw_sprite_scaled(const struct sprite *s, int x, int y, float scale, float angle);
void blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                const u8 *table, int src_pitch, int dst_pitch);
void blend_rect_scaled(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                       int dw, int dh, const u8 *table, int src_pitch, int dst_w, int dst_h);

#endif /* IGN_MENU_PAGES_H */
