/*
 * frontend.c - the front end: menu set up, assets and frame
 * (0x4029a0..0x404a60).
 */

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "frontend.h"
#include "frontend_state.h"
#include "input.h"
#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "menu.h"
#include "menu_engine.h"
#include "pic.h"
#include "settings.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

#define SETTINGS_FILE "ign_win.btz"

/* The current language's 25 pages (0x4be768); handlers compare the
 * current page with these. */
struct menu_page *g_frontend_pages[MENU_PAGE_TABLE];
/* Each language's row, as 0x404a60 copies it (0x4be8c8). */
struct menu_page *g_frontend_page_rows[MENU_LANGUAGES][MENU_PAGE_TABLE];
/* Each language's page list (0x4beb20). */
struct menu_page *const *g_frontend_lists[MENU_LANGUAGES];

s32 g_menu_mode_index;          /* 0x4be730, into g_menu_modes */
s32 g_menu_sound;               /* 0x4be874 */
s32 g_menu_language;            /* 0x4be60c */
u8 *g_menu_screen;              /* 0x4be840, the 320 x 200 menu buffer */
u8 *g_menu_palette;             /* 0x4be614, menu.col */
u8 *g_menu_table;               /* 0x4be86c, menu.tab */
u8 *g_render_target;            /* 0x498734 */
s32 *g_menu_camera;             /* 0x4be818, the 3D scene's camera */

/* Pictures without their header (0x34e bytes), as 0x403e80 reads them. */
u8 *g_pic_cars;                 /* 0x4be84c, bilar.pic, 186 wide */
u8 *g_pic_tracks;               /* 0x4be820, trk_spr.pic, 115 wide */
u8 *g_pic_logo;                 /* 0x4beb48, ign_logo.pic */
u8 *g_pic_car_select;           /* 0x4bf560, car_sel.pic */
u8 *g_pic_flags;                /* 0x4be8a8, flaggor.pic */

u8 *g_menu_black;               /* 0x4beb38, 0x300 bytes of 0 */
u8 *g_menu_white;               /* 0x4be580, 0x300 bytes of 0xff */
u8 *g_menu_palette_work;        /* 0x4be720 */
u8 *g_menu_buffer1;             /* 0x4be81c, 0xfb41 bytes */
u8 *g_menu_buffer2;             /* 0x4be8b8, 0xfb41 bytes */

/* The descriptor sprites are cut with (0x4be738), shared with the
 * handlers. */
struct tex_desc g_menu_desc;
struct sprite *g_sprite_logo;           /* 0x4bf558 */
struct sprite *g_sprite_panel;          /* 0x4be824 */
struct sprite *g_sprite_panel2;         /* 0x4be844 */
struct sprite *g_sprite_badges[12];     /* 0x4be7d0 */
struct sprite *g_sprite_car_select;     /* 0x4be800 */
struct sprite *g_sprite_car;            /* 0x4be848 */

s32 g_menu_sound_on;                    /* 0x4be5f0 */
s32 g_menu_volume;                      /* 0x4be590 */
static int s_mode_pending;              /* 0x4be65c */
static int s_load_language = 1;         /* 0x47dc68 */
static int s_sound_warned;              /* 0x47dc4c */
static s32 s_sound_params[3];           /* 0x4be808 */
static double s_last_ticks;             /* 0x4bf568 */

/* The menu's video modes (0x47dc30). */
const s32 g_menu_modes[3][2] = { { 320, 200 }, { 640, 480 }, { 800, 600 } };

/* Reads ign_win.btz over the settings when its version matches. */
static void settings_load(void)
{
    s32 version = 0;

    if (file_exists(SETTINGS_FILE) != 1) {
        return;
    }
    file_read_at(SETTINGS_FILE, &version, 4, 0);
    if (version == SETTINGS_VERSION) {
        file_read_at(SETTINGS_FILE, g_settings, SETTINGS_SIZE, 0);
    }
}

/* 0x403df0
 * Ticks of 1 / 0.036 ms since the last call: 0.01 when no time passed,
 * at most 10.8. */
double menu_ticks(void)
{
    double now = (double)(s32)g_frame_time * 0.036;
    double ticks = now - s_last_ticks;

    s_last_ticks = now;
    if (ticks == 0.0) {
        return 0.01;
    }
    return ticks > 10.8 ? 10.8 : ticks;
}

/* 0x403e70 */
static void menu_sound_tick(void)
{
    if (g_menu_sound) {
        sound_tick();
    }
}

/* 0x403da0 */
void menu_present(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];

    menu_sound_tick();
    video_blit(g_menu_screen, mode[0], 0, 0, mode[0], mode[1], 0, 0);
    video_flip();
}

/* 0x403b10 */
static void menu_set_mode(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    double one = 1.0;

    if (!s_mode_pending) {
        video_set_palette(g_menu_black);
        menu_sound_tick();
        video_clear();
        video_flip();
    }
    g_video_mode.bpp = 8;
    g_video_mode.flag = 1;
    g_video_mode.width = mode[0];
    g_video_mode.height = mode[1];
    video_set_mode();
    g_menu_camera[0x9c / 4] = mode[0] / 2;
    memcpy(&g_menu_camera[0x30 / 4], &one, sizeof(one));
    s_mode_pending = 0;
}

/* 0x403bc0
 * The original waits on Return in a loop of input_poll calls when no
 * sound card answers. */
static void menu_sound_init(void)
{
    int r;

    s_sound_params[0] = 5;
    s_sound_params[1] = 100;
    s_sound_params[2] = 2;
    r = sound_open(s_sound_params);
    if (r == 2 && !s_sound_warned) {
        int mini = g_fonts_menu[6];

        memset(g_menu_screen, 0, 64000);
        g_fonts[mini].align = FONT_ALIGN_CENTER;
        text_draw("NO SOUNDCARD FOUND, PLEASE REFER TO THE MANUAL", mini, 160, 90);
        text_draw("PRESS RETURN TO CONTINUE", mini, 160, 110);
        g_fonts[mini].align = FONT_ALIGN_LEFT;
        menu_present();
        input_poll();
        input_poll();
        input_poll();
        while (input_key_down(0x1c) != 1) {
            input_poll();
        }
        s_sound_warned = 1;
        return;
    }
    if (r == 0) {
        g_menu_sound_on = 0;
        g_menu_sound = 0;
    } else if (r == 1) {
        g_menu_sound_on = 1;
        g_menu_sound = 1;
        sound_load_bank("baltazar\\data\\sound\\", 0);
        g_menu_volume = 0xffff;
        sound_set_volume(0xffff);
    }
}

/* 0x404a60, after its static tables (0x409434 on); the tables are
 * menu_data.c. Returns the root page. */
struct menu_page *menu_setup(void)
{
    int lang, k;

    memcpy(g_frontend_pages, g_menu_page_table[0], sizeof(g_frontend_pages));
    settings_defaults();
    for (k = 0; k < MENU_PAGE_TABLE; k++) {
        settings_set(0x507 + 4 * k, g_frontend_pages[k]->selected);
    }
    memcpy(g_settings + 0x1bf, g_settings_block_1bf, sizeof(g_settings_block_1bf));
    settings_load();

    settings_set(SET_SCREEN_SIZE, 3 - settings_get(0x107));
    if (settings_get(0x587) < settings_get(SET_DIFFICULTY)) {
        settings_set(SET_DIFFICULTY, settings_get(0x587));
    }
    /* Each language's pages take their selection from the settings; the
     * first page starts at its first item, and DIFFICULTY (page 15)
     * offers as many levels as have been reached. */
    for (lang = 0; lang < MENU_LANGUAGES; lang++) {
        for (k = 0; k < MENU_PAGE_TABLE; k++) {
            struct menu_page *p = g_menu_page_table[lang][k];

            g_frontend_page_rows[lang][k] = p;
            p->selected = settings_get(0x507 + 4 * k);
        }
        g_frontend_page_rows[lang][0]->selected = 0;
        g_frontend_page_rows[lang][15]->items[0].max = settings_get(0x587);
    }
    for (lang = 0; lang < MENU_LANGUAGES; lang++) {
        g_frontend_lists[lang] = g_menu_pages[lang];
    }

    g_menu.screen_w = g_menu_modes[g_menu_mode_index][0];
    g_menu.screen_h = g_menu_modes[g_menu_mode_index][1];
    g_menu.root = g_menu_page_table[0][21];
    g_menu.callbacks = g_menu_callbacks;
    g_menu.key_pressed = g_key_pressed;
    g_menu.pages = g_frontend_lists[0];
    g_menu.bindable = g_menu_bindable;
    g_menu.tree = g_menu_tree;
    g_menu.tree_count = MENU_TREE_ENTRIES;
    g_menu.fonts = g_fonts_menu;
    g_menu.scaled = 0;
    g_menu.sound = g_menu_sound;
    g_menu.centre = 1;
    g_menu.keep_selection = 1;
    menu_engine_init();
    return g_menu.root;
}

static u8 *read_pixels(const char *name, u32 size)
{
    u8 *pixels = malloc(size);

    file_read_at(name, pixels, size, PIC_PIXELS);
    return pixels;
}

static struct sprite *cut(u8 *pixels, int w, int h, int stride, s32 hot_x, s32 hot_y)
{
    g_menu_desc.width = w;
    g_menu_desc.height = h;
    g_menu_desc.pixels = pixels;
    g_menu_desc.stride = stride;
    g_menu_desc.hot_x = hot_x;
    g_menu_desc.hot_y = hot_y;
    return sprite_create(&g_menu_desc, NULL);
}

/* 0x403e80
 * Buffers, the menu pictures, the menu tables and the sprites cut from
 * N_SYSGFX.PIC and the menu pictures. The front end's state words set
 * here are named as their users are ported. */
int menu_load_assets(void)
{
    u8 *sysgfx = (u8 *)pic_pixels(g_sysgfx);
    int row, col, n;

    frontend_reset_state();
    g_menu_black = calloc(0x300, 1);
    g_menu_white = malloc(0x300);
    memset(g_menu_white, 0xff, 0x300);
    g_menu_palette_work = malloc(0x300);
    g_menu_buffer1 = malloc(0xfb41);
    g_menu_buffer2 = malloc(0xfb41);
    g_menu_palette = malloc(0x300);
    file_read_at("baltazar\\data\\menu.col", g_menu_palette, 0x300, 8);
    g_pic_cars = read_pixels("baltazar\\data\\bilar.pic", 0x1e786);
    g_pic_tracks = read_pixels("baltazar\\data\\trk_spr.pic", 0x1374f);
    g_pic_logo = read_pixels("baltazar\\data\\ign_logo.pic", 0x2685);
    g_pic_car_select = read_pixels("baltazar\\data\\car_sel.pic", 0x49b6);
    g_pic_flags = read_pixels("baltazar\\data\\flaggor.pic", 0x978c);
    video_restore();
    menu_setup();
    frontend_reset_after_setup();

    memset(&g_menu_desc, 0, sizeof(g_menu_desc));
    g_sprite_logo = cut(g_pic_logo, 0xad, 0x39, 0xad, 0, 0);
    g_sprite_panel = cut(sysgfx + 0xfb4e - PIC_PIXELS, 0xfd, 0x3a, 0x100, 0x7e80, 0x1d00);
    g_sprite_panel2 = cut(sysgfx + 0x1354e - PIC_PIXELS, 0x7e, 0x6a, 0x100, 0x3f00, 0x3500);
    /* The twelve car badges, four to a row of 44 lines. */
    n = 0;
    for (row = 0; row < 3; row++) {
        for (col = 0; col < 4; col++) {
            g_sprite_badges[n++] = cut(sysgfx + row * 0x2c00 + col * 0x33,
                                       0x33, 0x2c, 0x100, 0x1900, 0x1600);
        }
    }
    g_sprite_car_select = cut(g_pic_car_select, 0xff, 0x4a, 0xff, 0, 0);
    frontend_reset_after_sprites();
    menu_ticks();
    return 1;
}

/* 0x4029a0 */
int menu_init(void)
{
    const s32 *mode;
    const char *loading;
    int small = g_fonts_menu[7];
    u8 *tab_raw;

    g_menu_mode_index = 0;
    g_menu_palette = malloc(0x300);
    file_read_at("baltazar\\data\\menu.col", g_menu_palette, 0x300, 8);
    g_menu_screen = malloc(0xfb41);
    g_menu_camera = scene_create();
    s_mode_pending = 1;
    menu_set_mode();
    video_set_palette(g_menu_palette);
    if (s_load_language) {
        s32 version = 0;
        int i;

        if (file_exists(SETTINGS_FILE) == 1
            && (file_read_at(SETTINGS_FILE, &version, 4, 0), version == SETTINGS_VERSION)) {
            file_read_at(SETTINGS_FILE, &g_menu_language, 4, SET_LANGUAGE);
        } else {
            g_menu_language = 0;
        }
        for (i = 0; i < FONT_MAX; i++) {
            g_fonts[i].proportional = 1;
            g_fonts[i].align = FONT_ALIGN_LEFT;
        }
    }
    /* menu.tab goes at a 64 KB boundary in the original, for the
     * rasteriser's addressing. */
    tab_raw = malloc(0x10000);
    file_read_at("baltazar\\data\\menu.tab", tab_raw, 0x10000, 0);
    g_menu_table = tab_raw;
    g_gfx_color_table = tab_raw;

    mode = g_menu_modes[g_menu_mode_index];
    set_target(g_menu_screen, mode[0], mode[0], mode[1], 8);
    memset(g_menu_screen, 0, 64000);
    loading = ui_text(0x47e0a0 + 200 * g_menu_language);
    text_draw(loading, small, 160 - text_width(loading, small) / 2, 0x5e);
    menu_present();
    raster_set_grid(2000, 2000, 11);
    g_menu_camera[0x3c / 4] = 0;
    g_render_target = g_menu_screen;
    raster_set_screen(mode[0], mode[1], 0);
    menu_sound_init();
    input_poll();
    return menu_load_assets();
}
