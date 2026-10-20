/*
 * texture.h - Lisa 2 textures: images of up to 256 x 256 pixels that
 * sprites and meshes draw from, addressed by id.
 *
 * The original copies each texture into 256 x 256 pages aligned to
 * 64 KB, so the rasteriser can address a texel as page | v << 8 | u.
 * The port keeps a plain copy per texture instead; OpenGL does the
 * addressing.
 */

#ifndef IGN_LISA_TEXTURE_H
#define IGN_LISA_TEXTURE_H

#include "types.h"

/* The 0x40 byte descriptor obj_create takes. Field offsets are the
 * original's; this build's pointers may be wider. */
struct tex_desc {
    char *name;     /* +0x00 */
    s32 width;      /* +0x04, 1..256 */
    s32 height;     /* +0x08, 1..256 */
    s32 flags;      /* +0x0c */
    u8 *pixels;     /* +0x10 */
    s32 stride;     /* +0x14 */
    s32 hot_x;      /* +0x18 */
    s32 hot_y;      /* +0x1c */
    s32 extra[8];   /* +0x20 */
};

void tex_init(void);

/* obj_create (0x461360):
 *   desc, id 0     new texture; returns its id
 *   desc, id > 0   redefines texture id; returns id
 *   NULL, id > 0   frees texture id; returns 0
 * The pixels are copied; the caller's buffer can go. Returns 0 for an
 * id out of range. */
int obj_create(const struct tex_desc *desc, int id);

/* The descriptor of texture `id`, with hot_x and hot_y cleared, or the
 * "default" descriptor when there is none. Returns 0 for a valid id,
 * `id` otherwise. */
int tex_get_info(struct tex_desc *out, int id);

#endif /* IGN_LISA_TEXTURE_H */
