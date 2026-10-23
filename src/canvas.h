/*
 * canvas.h - the game's 8 bit screens on the GPU.
 *
 * The game draws into 8 bit buffers of the original's size (the menu's
 * 320 x 200 screen and its background). A buffer registered here is a
 * GPU render target instead: its logical size stays the original's, its
 * pixels are drawn at the internal resolution, and sprites, text,
 * blended rectangles and the 3D draw into it on the GPU. Drawing that
 * combines a source colour with the colour under it goes through the
 * game's 256 x 256 tables, as in the original.
 *
 * Without a GL context (the tests) nothing is registered and the game
 * draws into its buffers on the CPU as before.
 */

#ifndef IGN_CANVAS_H
#define IGN_CANVAS_H

#include "types.h"

/* Internal resolutions, as the RESOLUTION option numbers them. */
enum {
    CANVAS_320X200 = 0,
    CANVAS_640X480 = 1,
    CANVAS_800X600 = 2,
    CANVAS_NATIVE = 3
};

int canvas_init(void);
/* Hardware (the default) draws the 2D into GPU surfaces; software
 * draws it into the game's buffers on the CPU as the original did, the
 * 3D read back into them. Switching clears the surfaces. */
void canvas_set_hardware(int on);
int canvas_hardware(void);
int canvas_ready(void);
/* Picks the internal resolution; surfaces are made again, cleared. */
void canvas_set_resolution(int mode);
int canvas_resolution(void);
/* Overrides the setting (the --res option). */
void canvas_force_resolution(int mode);
/* The internal resolution's height and the width a 4:3 picture of it
 * takes (320 x 200 for the original's). */
void canvas_internal_size(int *w43, int *h);

/* Makes `key`, a buffer of lw x lh, a GPU surface. */
void canvas_register(u8 *key, int lw, int lh);
void canvas_unregister(u8 *key);
int canvas_is_gpu(const u8 *key);
/* A surface's margins about its logical screen in NATIVE, in logical
 * pixels (0 for none), and the shape it is shown at (`fallback` without
 * margins). */
void canvas_extent(const u8 *key, float *mx, float *my);
float canvas_aspect(const u8 *key, float fallback);
void canvas_sync_drawable(void);

/* memset and memcpy for buffers that may be surfaces. */
void canvas_clear(u8 *key, u8 index, size_t bytes);
void canvas_copy(u8 *dst, const u8 *src, size_t bytes);
/* Puts a CPU image of the surface's logical size into it. */
void canvas_put(u8 *dst, const u8 *pixels, int pitch);

/* A sprite into the current target (g_gfx_target), when it is a
 * surface: `pixels` w x h at `stride`, its top left at (x, y) in
 * logical pixels (24.8), scaled and turned by `m` (16.16, 2 x 2) about
 * (hx, hy) if not NULL, through g_gfx_color_table. Returns 0 when the
 * target is not a surface. */
int canvas_sprite(const u8 *pixels, int w, int h, int stride, s32 x, s32 y, s32 hx, s32 hy,
                  const s32 *m);
/* Forgets the GPU copy of a sprite's pixels. */
void canvas_forget(const u8 *pixels);

/* blend_rect onto a surface: dst = table[src << 8 | dst] over the
 * rectangle (x0, y0)..(x1, y1) of src, put at (dx, dy). src may be a
 * surface or a CPU buffer of `src_pitch`. */
int canvas_blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                      const u8 *table, int src_pitch);
/* dst = table[level << 8 | dst] for each pixel, `levels` a logical
 * size CPU image. */
void canvas_remap(u8 *dst, const u8 *levels, const u8 *table);

/* Binds a surface to draw 3D into, with a depth buffer: returns its
 * GL framebuffer, and its physical size in *pw, *ph; 0 when `key` is
 * not a surface. `clear_depth` clears the depth. */
unsigned canvas_bind(u8 *key, int *pw, int *ph, int clear_depth);
/* Its colour texture (R8, row 0 at the top), and a copy of what it
 * holds now, for drawing that reads the pixel under it. */
unsigned canvas_texture(const u8 *key);
unsigned canvas_snapshot(const u8 *key);
/* The same for a logical rectangle of it only. */
unsigned canvas_snapshot_rect(const u8 *key, float x0, float y0, float x1, float y1);

#endif /* IGN_CANVAS_H */
