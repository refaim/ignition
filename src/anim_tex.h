/*
 * anim_tex.h - the animated textures.
 */

#ifndef IGN_ANIM_TEX_H
#define IGN_ANIM_TEX_H

#include "types.h"

void anim_tex_free(void);
void anim_tex_add(const s32 *mesh, int tex);
void anim_tex_step(void);

#endif /* IGN_ANIM_TEX_H */
