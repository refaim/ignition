/*
 * menu.h - the front end: its data driven menu pages and the engine that
 * runs them. See NOTES.md, "Front end".
 */

#ifndef IGN_MENU_H
#define IGN_MENU_H

#include "types.h"

enum {
    MENU_LANGUAGES = 6,
    MENU_PAGE_SLOTS = 30,
    MENU_PAGE_TABLE = 25,
    MENU_TREE_ENTRIES = 25,
    MENU_CALLBACKS = 5,
    SETTINGS_SIZE = 0x597
};

/* One look of an item: a picture and a line of text, positioned from the
 * screen centre. The original record is 0x18 bytes, packed. */
struct menu_visual {
    u8 *pixels;         /* +0x00, set by the menu loader */
    s16 x, y;           /* +0x04 */
    s16 width, height;  /* +0x08, < 0 for the screen's */
    s16 stride;         /* +0x0c */
    const char *text;   /* +0x0e */
    s16 text_x, text_y; /* +0x12 */
    s16 font;           /* +0x16, index into the menu fonts */
};

/* A key binding or text field. 0x18 bytes in the original. */
struct menu_choice {
    struct menu_visual *visual;     /* +0x00 */
    u8 *key;                        /* +0x04, in g_settings */
    s16 words[8];                   /* +0x08; words[6] (+0x14) is the kind */
};

/* An entry of a page. 0x1c bytes in the original, packed. */
struct menu_item {
    s32 *value;                     /* +0x00, in g_settings, or NULL */
    s32 min, max;                   /* +0x04 */
    struct menu_visual *visuals;    /* +0x0c */
    s32 f10;                        /* +0x10 */
    s16 f14;                        /* +0x14 */
    struct menu_choice *choice;     /* +0x16 */
    s16 kind;                       /* +0x1a */
};

typedef int (*menu_handler)(void);
typedef int (*menu_callback)(int arg0, int arg1);

/* 0x18 bytes in the original. */
struct menu_page {
    menu_handler handler_a;         /* +0x00 */
    menu_handler handler_b;         /* +0x04 */
    const s32 *style;               /* +0x08 */
    s32 count;                      /* +0x0c */
    struct menu_item *items;        /* +0x10 */
    s32 selected;                   /* +0x14 */
};

/* The settings, saved as ign_win.btz (0x4bf580). */
extern u8 g_settings[SETTINGS_SIZE + 0x10];

extern struct menu_page *const g_menu_pages[MENU_LANGUAGES][MENU_PAGE_SLOTS];
/* The 25 pages of 0x4be768, for each language. */
extern struct menu_page *const g_menu_page_table[MENU_LANGUAGES][MENU_PAGE_TABLE];
extern const s16 g_menu_tree[MENU_TREE_ENTRIES][32];
extern menu_callback const g_menu_callbacks[MENU_CALLBACKS];
extern const s32 g_menu_page_style[16];

int menu_page_handler_a(void);      /* 0x409fb0 */
int menu_page_handler_b(void);      /* 0x409610 */
int menu_cb_4045b0(int arg0, int arg1);
int menu_cb_4045e0(int arg0, int arg1);
int menu_cb_404610(int arg0, int arg1);
int menu_cb_404740(int arg0, int arg1);
int menu_cb_404790(int arg0, int arg1);

#endif /* IGN_MENU_H */
