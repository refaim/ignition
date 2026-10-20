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
/* A screen space command, as the original's rasteriser takes them
 * (0x44f0e9), in pixels and texels. A sprite (type 7) is given as two
 * type 18 triangles. */
enum {
    RASTER_FLAT = 15,           /* colour */
    RASTER_TEXTURE = 17,        /* page[v][u] */
    RASTER_TEXTURE_TABLE = 18,  /* table[page[v][u]][dst] */
    RASTER_FLAT_TABLE = 19      /* table[colour][dst] */
};

struct raster_cmd {
    int type;
    int page;                   /* in the page set */
    const u8 *table;            /* 256 x 256, [a << 8 | dst] */
    u8 colour;
    float x[3], y[3];           /* pixels */
    float u[3], v[3];           /* texels */
};

/* The pages raster commands index: `count` pages of 256 x 256 from
 * `pixels`. Kept until gl3d_flush. */
void gl3d_raster_pages(const u8 *pixels, int count);
/* Draws commands in order over an 8 bit target of w x h. */
void gl3d_raster(u8 *target, int w, int h, int pitch, const struct raster_cmd *cmds, int count);
/* Forgets the meshes and textures it holds, before their memory goes. */
void gl3d_flush(void);

#endif /* IGN_GL3D_H */
