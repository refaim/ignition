/*
 * gl3d.h - draws the scene's objects with OpenGL, in place of the
 * original's software pipeline and rasteriser (0x4466d0, 0x44f0e9).
 *
 * The meshes' faces become triangles with 8 bit colour indices; the
 * frame is drawn into an index buffer and read back over an 8 bit
 * screen, so it mixes with the 2D drawing as the original's did.
 */

#ifndef IGN_GL3D_H
#define IGN_GL3D_H

#include "scene.h"
#include "types.h"

/* Starts a frame of w x h pixels, empty. */
void gl3d_begin(int w, int h);
/* Draws objects as seen from `sc`, the original's view window placed
 * at (ox, oy) on the frame. */
void gl3d_draw(const struct scene *sc, struct scene_obj *const *objs, int count, int ox, int oy);
/* Copies what was drawn over `screen` (w x h at `pitch`). */
void gl3d_end(u8 *screen, int pitch);
/* Forgets the meshes and textures it holds, before their memory goes. */
void gl3d_flush(void);

#endif /* IGN_GL3D_H */
