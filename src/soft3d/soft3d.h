/*
 * soft3d/soft3d.h - the original's software 3D: the scene pipeline
 * (0x4466d0) and the rasteriser (0x44f0e9), drawing 8 bit colour
 * indices into a CPU buffer at the screen's resolution. With
 * --soft3d it draws the race and the front end's 3D in place of gl3d.c.
 */

#ifndef IGN_SOFT3D_H
#define IGN_SOFT3D_H

#include "scene.h"
#include "types.h"

/* --soft3d: the 3D is drawn by this pipeline. */
extern int g_soft3d;

struct sw_raster;

/* 0x44f070: the screen the rasteriser draws into, w x h, w a row. */
int sw_raster_set_screen(int w, int h);
/* 0x44f0e9: draws a context's command list. */
int sw_raster_draw(const struct sw_raster *ctx);

/* The tables faces go through: the level's .SHD (0x63b5f0), the colour
 * key (0x63c5b4) and the .PAN (0x63c5f4). */
void soft3d_set_tables(const u8 *shd, const u8 *key, const u8 *pan);

/* 0x4466d0: runs g_scene's stages over g_scene_visible and returns the
 * commands far to near, ended by NULL, the clip in g_sw_raster. */
void *const *soft3d_scene_render(void);

/* Draws g_scene into `target` (a screen `pitch` wide and `height` high,
 * as the rasteriser was set to), clipped to x0..x1, y0..y1 (the ends
 * included), as the race's view and the front end's car do: 0x4466d0
 * then 0x44f0e9. `table` is the context's table, which the flat
 * commands draw through. */
void soft3d_draw(u8 *target, int pitch, int height, int x0, int y0, int x1, int y1,
                 const u8 *table);

/* MIP MAPPING (mip.c): the race's texture slots (0x41b470's calls of
 * 0x447280), and what the scene frees (0x446c30). */
void soft3d_race_textures(void);
void soft3d_scene_freed(void);

/* The port's glue (port.c). */
struct raster_cmd;
void soft3d_race_view(int first);
void soft3d_race_fill(int y0, int y1, int index);
void soft3d_menu_car(int ox, int oy, int w, int h);
void soft3d_raster(u8 *target, int w, int h, int pitch, const struct raster_cmd *cmds, int count,
                   const u8 *pages);

#endif /* IGN_SOFT3D_H */
