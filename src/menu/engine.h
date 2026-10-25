/*
 * menu/engine.h - the engine that runs the front end's menu pages.
 */

#ifndef IGN_MENU_ENGINE_H
#define IGN_MENU_ENGINE_H

#include "lisa/sprite.h"
#include "menu/menu.h"
#include "types.h"

/* What the front end gives the engine (0x4c50d0, filled at the end of
 * 0x404a60). */
struct menu_context {
    s32 screen_w;                   /* 0x4c50d0 */
    s32 screen_h;                   /* 0x4c50d4 */
    struct menu_page *root;         /* 0x4c50d8 */
    menu_callback const *callbacks; /* 0x4c50dc */
    u8 *key_pressed;                /* 0x4c50e0 */
    const u8 *bindable;             /* 0x4c50e4 */
    struct menu_page *const *pages; /* 0x4c50e8, the language's page list */
    const s16 (*tree)[32];          /* 0x4c50ec */
    s32 tree_count;                 /* 0x4c50f0 */
    s32 sound;                      /* 0x4c50f4 */
    int *fonts;                     /* 0x4c50f8 */
    s32 centre;                     /* 0x4c50fc: positions from the centre */
    s32 scaled;                     /* 0x4c5100: positions in 3200 x 2400 */
    s32 keep_selection;             /* 0x4c5104 */
};

/* The engine's state as the handlers see it (0x4c52a0). */
struct menu_snapshot {
    struct menu_page *page_stack[16];   /* 0x4c52a0 */
    s32 select_stack[16];               /* 0x4c52e0 */
    struct menu_page *page;             /* 0x4c5320 */
    struct menu_page *prev_page;        /* 0x4c5324 */
    s32 depth;                          /* 0x4c5328 */
    s32 selected;                       /* 0x4c532c */
    s32 prev_selected;                  /* 0x4c5330 */
    s16 edit;                           /* 0x4c5334 */
    s32 page_changed;                   /* 0x4c5336, set on a page change,
                                           cleared the frame after */
};

extern struct menu_context g_menu;
extern struct menu_snapshot g_menu_view;

int menu_engine_init(void);
/* The port's: the key, and how many presses of it, a tap at (fx, fy)
 * (shares of the drawable) asks of the current page; 0 for none. */
int menu_touch(float fx, float fy, int *count);
/* And presses those keys: a tap on the Vita's screen, a click. */
void menu_tap(float fx, float fy);
int menu_engine_frame(void);
int menu_engine_free(void);
int menu_set_delay(s32 ms);
int menu_change_page(int dir);
void menu_draw_visual(const struct menu_visual *v, const struct sprite *sprite);
int menu_draw_items(void);

/* An item's label (0) or value (1) sprite, as menu_item_sprite made it. */
struct sprite *menu_item_sprite_of(int index, int which);

/* How much of the last 25 ms step had passed (0x4c4e48). */
double menu_step_part(void);

/* The text being typed into a text field (0x4c4fc8). */
const char *menu_edit_text(void);

int menu_spread_y(int y);
int menu_lift_y(void);
int menu_place_y(int y);

#endif /* IGN_MENU_ENGINE_H */
