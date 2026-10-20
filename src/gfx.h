/*
 * gfx.h - OpenGL renderer.
 *
 * Replaces the original's software rasteriser and DirectDraw. The 2D
 * screens (menus, HUD overlays) are still drawn by the game into an 8 bit
 * surface at their original size; gfx_draw_screen shows that surface,
 * through its palette, centred at its original aspect ratio in the window.
 * 3D scenes are drawn with OpenGL directly.
 *
 * Must not include platform.h. Expects a current OpenGL 3.2 core context.
 */

#ifndef IGN_GFX_H
#define IGN_GFX_H

#include "types.h"

int gfx_init(void);
void gfx_shutdown(void);

/* Starts a frame on a `width` x `height` pixel drawable: clears it to
 * black and sets the viewport. */
void gfx_begin_frame(int width, int height);

/* Draws an 8 bit surface of `w` x `h` pixels with a 256 entry RGB palette
 * (8 bits a channel), scaled to fill the drawable's height at a display
 * aspect of `aspect` (width / height) and centred. Index 0 is drawn like
 * any other colour. */
void gfx_draw_screen(const u8 *pixels, int w, int h, int pitch,
                     const u8 *palette, float aspect);

#endif /* IGN_GFX_H */
