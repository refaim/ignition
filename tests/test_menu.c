/*
 * test_menu.c - drives the menu engine over the generated tables, with
 * the context 0x404a60 sets up, and checks the navigation.
 */

#include <stdio.h>
#include <string.h>

#include "dik.h"
#include "input.h"
#include "lisa/sprite.h"
#include "lisa/texture.h"
#include "menu.h"
#include "menu_engine.h"
#include "startup.h"

static int s_failed;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failed = 1; \
        } \
    } while (0)

/* A frame with `key` pressed. Extended keys arrive in the trigger table
 * as well, as input_poll sets both. */
static void press(int key)
{
    g_key_pressed[key] = 1;
    g_key_trigger[key] = 1;
    menu_engine_frame();
}

int main(void)
{
    static int fonts[8];

    tex_init();
    sprite_init();
    g_menu.screen_w = 320;
    g_menu.screen_h = 200;
    g_menu.root = g_menu_page_table[0][21];     /* 0x481868 */
    g_menu.callbacks = g_menu_callbacks;
    g_menu.key_pressed = g_key_pressed;
    g_menu.bindable = g_menu_bindable;
    g_menu.pages = g_menu_pages[0];
    g_menu.tree = g_menu_tree;
    g_menu.tree_count = 25;
    g_menu.fonts = fonts;
    g_menu.centre = 1;
    g_menu.keep_selection = 1;

    /* The engine alone: no page handlers. */
    {
        int lang, k;

        for (lang = 0; lang < MENU_LANGUAGES; lang++) {
            for (k = 0; k < MENU_PAGE_SLOTS; k++) {
                if (g_menu_pages[lang][k] != NULL) {
                    g_menu_pages[lang][k]->handler_a = NULL;
                    g_menu_pages[lang][k]->handler_b = NULL;
                }
            }
            for (k = 0; k < MENU_PAGE_TABLE; k++) {
                g_menu_page_table[lang][k]->handler_a = NULL;
                g_menu_page_table[lang][k]->handler_b = NULL;
            }
        }
    }
    CHECK(menu_engine_init() == 1);
    CHECK(g_menu_view.depth == 0 && g_menu_view.page == g_menu.root);

    /* Return on the root page follows the tree's first step, code 1:
     * the main menu, 0x481850. */
    press(DIK_RETURN);
    CHECK(g_menu_view.depth == 1);
    CHECK(g_menu_view.page == g_menu_pages[0][1]);
    CHECK(g_menu_view.page->count == 5);
    CHECK(g_menu_view.selected == 0);

    /* Down (0xd0, an extended key) wraps through the five items. */
    press(DIK_DOWN);
    CHECK(g_menu_view.selected == 1);
    press(DIK_UP);
    CHECK(g_menu_view.selected == 0);
    press(DIK_UP);
    CHECK(g_menu_view.selected == 4);

    /* Escape goes back to the root, then leaves the menu. */
    press(DIK_ESCAPE);
    CHECK(g_menu_view.depth == 0 && g_menu_view.page == g_menu.root);
    g_key_pressed[DIK_ESCAPE] = 1;
    CHECK(menu_engine_frame() == -1);

    puts(s_failed ? "FAIL" : "ok");
    return s_failed;
}
