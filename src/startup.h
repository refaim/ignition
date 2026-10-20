/*
 * startup.h - the game's video and data set up.
 */

#ifndef IGN_STARTUP_H
#define IGN_STARTUP_H

#include "lisa/sprite.h"
#include "types.h"

extern u8 *g_identity_table;
extern int g_game_pool;
extern int g_fatal_error;
extern char g_error_text[256];
extern s32 g_screen_width;
extern s32 g_screen_height;
extern u8 g_back_buffer[320 * 200];
extern u8 *g_sysgfx;
extern u8 *g_sysgfx2;
extern u8 *g_sys_palette;
extern int g_fonts_menu[8];
extern struct sprite *g_position_icons[4];
extern struct sprite *g_bolt_icon;

/* x, y, width, height of the position icons in N_SYSGFX.PIC (0x497bb8). */
extern const s32 g_position_icon_rect[4];
extern s32 g_screen_size;
extern u8 *g_menu_background;
extern u8 g_menu_remap[256];
extern u8 *g_cone_table;

int game_video_init(void);

/* Sets the menu's video mode from g_screen_size. */
int game_set_menu_mode(void);

#endif /* IGN_STARTUP_H */
