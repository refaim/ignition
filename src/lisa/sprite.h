/*
 * sprite.h - Lisa 2 sprites: a texture with a hot spot, drawn into the 2D
 * target through the colour table.
 *
 * Positions and hot spots are 24.8 fixed point. A sprite is drawn with
 * its hot spot at the position.
 */

#ifndef IGN_LISA_SPRITE_H
#define IGN_LISA_SPRITE_H

#include "lisa/texture.h"
#include "types.h"

struct sprite {
    s32 tex;        /* texture id; 0 when the record is free */
    s32 hot_x;      /* 24.8 */
    s32 hot_y;      /* 24.8 */
};

struct sprite_pos {
    s32 x;          /* 24.8 */
    s32 y;          /* 24.8 */
};

/* Scale and rotation for a drawn sprite. */
struct sprite_xform {
    float scale;
    float angle;    /* radians */
};

void sprite_init(void);

/* 0x456d40, through 0x50ebc0:
 *   desc, NULL     a new sprite
 *   desc, sprite   redefines the sprite
 *   NULL, sprite   frees the sprite; returns NULL
 * Returns NULL when no record is free. */
struct sprite *sprite_create(const struct tex_desc *desc, struct sprite *sprite);

/* 0x456d20, through 0x50ebbc. `xform` NULL draws unscaled. */
int sprite_draw(const struct sprite *sprite, const struct sprite_pos *pos,
                const struct sprite_xform *xform);

#endif /* IGN_LISA_SPRITE_H */
