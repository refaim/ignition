/*
 * frontend.h - the front end: menu set up, assets and frame.
 */

#ifndef IGN_FRONTEND_H
#define IGN_FRONTEND_H

#include "lisa/sprite.h"
#include "menu.h"
#include "types.h"

extern struct menu_page *g_frontend_pages[MENU_PAGE_TABLE];
extern struct menu_page *g_frontend_page_rows[MENU_LANGUAGES][MENU_PAGE_TABLE];
extern struct menu_page *const *g_frontend_lists[MENU_LANGUAGES];
extern s32 g_menu_mode_index;
extern s32 g_menu_sound;
extern const s32 g_menu_modes[3][2];

extern u8 *g_menu_screen;
extern u8 *g_menu_palette;
extern u8 *g_menu_table;
extern s32 *g_menu_camera;
extern s32 g_menu_language;
extern struct sprite *g_sprite_car;

struct menu_page *menu_setup(void);
int menu_load_assets(void);
void menu_present(void);
double menu_ticks(void);
int menu_init(void);
int menu_frame(void);
void palette_lerp(const u8 *from, const u8 *to, u8 *out, int t);
void menu_message(const char *text, double time);

#endif /* IGN_FRONTEND_H */
