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
    u8 *data;                       /* +0x04, in g_settings: the key code,
                                       or the text */
    s16 words[8];                   /* +0x08; the low byte of words[4]
                                       (+0x10) is a text field's size,
                                       words[6] (+0x14) the kind */
};

enum menu_choice_kind {
    MENU_CHOICE_TEXT = 1,           /* edit on Return */
    MENU_CHOICE_TEXT_NOW = 2,       /* edit on entering the page */
    MENU_CHOICE_KEY = 3             /* the next key pressed */
};

/* An entry of a page. 0x1c bytes in the original, packed. */
struct menu_item {
    s32 *value;                     /* +0x00, in g_settings, or NULL */
    s32 min, max;                   /* +0x04 */
    struct menu_visual *visuals;    /* +0x0c */
    s16 action;                     /* +0x10: a callback (1 based), or -1
                                       to go back a page */
    s16 action_arg;                 /* +0x12 */
    s16 f14;                        /* +0x14 */
    struct menu_choice *choice;     /* +0x16 */
    s16 kind;                       /* +0x1a */
};

/* Handler A runs once a frame, handler B for each key pressed with the
 * key's action from the key map; B returning MENU_KEY_TAKEN stops the
 * engine handling that key. */
typedef int (*menu_handler)(void);
typedef int (*menu_key_handler)(int key, int action);

enum { MENU_KEY_TAKEN = 0x441 };
typedef int (*menu_callback)(int arg0, int arg1);

/* 0x18 bytes in the original. */
struct menu_page {
    menu_handler handler_a;         /* +0x00 */
    menu_key_handler handler_b;     /* +0x04 */
    const s8 *keymap;               /* +0x08: scan code to action */
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
/* Writable: the front end binds player keys into it. */
extern s8 g_menu_keymap[256];                   /* 0x47dd70 */
extern const s8 g_menu_keymap_default[256];     /* 0x4931c8 */
extern const u8 g_menu_text_chars[0x54];        /* 0x4932c8, 0xff for none */
extern const u8 g_menu_bindable[256];           /* 0x47de70 */

/* String pointer runs the front end indexes by address (0x47dfe8 and
 * 0x47e560); ui_text takes the original address of an entry. */
extern const char *const g_ui_text[0x13d];
extern const char *const g_ui_names[11];

static inline const char *ui_text(u32 addr)
{
    return g_ui_text[(addr - 0x47dfe8) / 4];
}

static inline const char *ui_name(u32 addr)
{
    return g_ui_names[(addr - 0x47e560) / 4];
}

/* Copied over the settings from offset 0x1bf at start up (0x47e590). */
extern const u8 g_settings_block_1bf[0x348];

/* What a key does on a page, from the key map. */
enum menu_key_action {
    MENU_KEY_BACK = -6,
    MENU_KEY_MIN = -5,
    MENU_KEY_DEC = -4,              /* stops at the minimum */
    MENU_KEY_DEC_WRAP = -3,
    MENU_KEY_UP = -2,               /* stops at the first item */
    MENU_KEY_UP_WRAP = -1,
    MENU_KEY_DOWN_WRAP = 1,
    MENU_KEY_DOWN = 2,              /* stops at the last item */
    MENU_KEY_INC_WRAP = 3,
    MENU_KEY_INC = 4,               /* stops at the maximum */
    MENU_KEY_MAX = 5,
    MENU_KEY_ACTIVATE = 6
};

int menu_page_handler_a(void);      /* 0x409fb0 */
int menu_page_handler_b(int key, int action);   /* 0x409610 */
int menu_cb_4045b0(int arg0, int arg1);
int menu_cb_4045e0(int arg0, int arg1);
int menu_cb_404610(int arg0, int arg1);
int menu_cb_404740(int arg0, int arg1);
int menu_cb_404790(int arg0, int arg1);

#endif /* IGN_MENU_H */
