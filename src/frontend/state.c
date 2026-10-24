/*
 * frontend/state.c - the front end state words 0x403e80 resets.
 */

#include <string.h>

#include "frontend/frontend.h"
#include "frontend/state.h"

s32 fe_4be828, fe_4be72c, fe_4be634, fe_4be5a0;
double fe_4be5a8, fe_4be8c0, fe_4be8a0;
double fe_4be850, fe_4be560, fe_4be598;
s32 fe_4be56c, fe_4be608, fe_4be62c, fe_4be8bc, fe_4be8b0, fe_4be8b4;
s32 fe_4bf564, fe_4bf554, fe_4bf55c, fe_4be728, fe_4be724, fe_4bf550;
s32 fe_4be618[5], fe_4be6d8, fe_4be87c, fe_4be5fc, fe_4be58c;
s32 fe_4be5e0[4], fe_4be594, fe_4be588, fe_4be830[4];
s32 fe_4be600, fe_4be568;
s32 fe_4be710, fe_4be718;
s32 fe_47dc48 = 1;
struct sound_voice *fe_4be584;
void *fe_4be880[6];
s32 fe_4be5dc, fe_4be75c;
void *fe_4be5f8, *fe_4be630, *fe_4be898, *fe_4be804, *fe_4be604, *fe_4be5f4, *fe_4be658;

/* 0x403e80, before the buffers */
void frontend_reset_state(void)
{
    fe_4be828 = 0;
    fe_4be72c = 0;
    fe_4be5a8 = 0.0;
    fe_4be8c0 = 0.0;
    fe_4be8a0 = 0.0;
    fe_4be634 = 0;
    fe_4be5a0 = 1;
}

/* 0x403e80, after menu_setup */
void frontend_reset_after_setup(void)
{
    fe_4be850 = 0.0;
    fe_4be560 = 0.0;
    fe_4be598 = 0.0;
    fe_4be56c = 0;
    fe_4be608 = 0;
    fe_4be62c = -99;
    fe_4be8bc = -99;
    fe_4be8b0 = 0;
    fe_4be8b4 = 0;
    fe_4bf564 = 0;
    g_sprite_car = NULL;
    fe_4bf554 = 0;
    fe_4bf55c = 0;
    fe_4be728 = 0;
    fe_4be724 = 0;
    fe_4bf550 = 1;
}

/* 0x403e80, after the sprites */
void frontend_reset_after_sprites(void)
{
    memset(fe_4be618, 0, sizeof(fe_4be618));
    fe_4be6d8 = 0;
    fe_4be87c = 0;
    fe_4be5fc = 0;
    fe_4be58c = 1;
    memset(fe_4be5e0, 0, sizeof(fe_4be5e0));
    fe_4be594 = -1;
    fe_4be588 = -1;
    memset(fe_4be830, 0, sizeof(fe_4be830));
}
