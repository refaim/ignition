/*
 * frontend_state.h - front end state words not identified yet, named by
 * their address in IGN_WIN.EXE. Rename them as their users are ported.
 */

#ifndef IGN_FRONTEND_STATE_H
#define IGN_FRONTEND_STATE_H

#include "lisa/sprite.h"
#include "types.h"

extern s32 fe_4be828, fe_4be72c, fe_4be634, fe_4be5a0;
extern double fe_4be5a8, fe_4be8c0, fe_4be8a0;
extern double fe_4be850, fe_4be560, fe_4be598;
extern s32 fe_4be56c, fe_4be608, fe_4be62c, fe_4be8bc, fe_4be8b0, fe_4be8b4;
extern s32 fe_4bf564, fe_4bf554, fe_4bf55c, fe_4be728, fe_4be724, fe_4bf550;
extern s32 fe_4be618[5], fe_4be6d8, fe_4be87c, fe_4be5fc, fe_4be58c;
extern s32 fe_4be5e0[4], fe_4be594, fe_4be588, fe_4be830[4];

extern s32 fe_4be600, fe_4be568;
extern s32 fe_4be710, fe_4be718, fe_47dc48;
extern void *fe_4be584;
extern void *fe_4be880[6];

void frontend_reset_state(void);
void frontend_reset_after_setup(void);
void frontend_reset_after_sprites(void);

#endif /* IGN_FRONTEND_STATE_H */
