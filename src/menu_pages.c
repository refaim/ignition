/*
 * menu_pages.c - what the front end draws for its pages: handler A
 * (0x409fb0), which every page runs once a frame.
 *
 * Pages are named by their index in the current language's table
 * (g_frontend_pages, 0x4be768): 4 and 5 choose player 1's and player
 * 2's car, 6 the track, 8 is the network game, 16 the best times, 21
 * the language.
 */

#include <math.h>
#include <string.h>

#include "app.h"
#include "frontend.h"
#include "frontend_state.h"
#include "input.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "menu.h"
#include "lisa/texture.h"
#include "menu_engine.h"
#include "menu_pages.h"
#include "menu_panels.h"
#include "settings.h"
#include "startup.h"
#include "stubs.h"

enum {
    PAGE_CAR_P1 = 4,
    PAGE_CAR_P2 = 5,
    PAGE_TRACK = 6,
    PAGE_NETWORK = 8,
    PAGE_CD = 10,
    PAGE_DIFFICULTY = 15,
    PAGE_BEST_TIMES = 16,
    PAGE_LANGUAGE = 21
};

s32 g_car_order[11];                    /* 0x4be6e0 */
s32 g_car_count;                        /* 0x4be858 */
struct sprite *g_sprite_track;          /* 0x4bf55c */
struct sprite *g_sprite_flags[5];       /* 0x4be618 */

static double s_background_t;           /* 0x4bf570 */

static struct menu_page *page(int k)
{
    return g_frontend_pages[k];
}

/* 0x40b7a0 */
void draw_sprite_at(const struct sprite *s, int x, int y)
{
    struct sprite_pos pos;

    pos.x = x << 8;
    pos.y = y << 8;
    sprite_draw(s, &pos, NULL);
}

/* 0x40b7d0 */
void draw_sprite_scaled(const struct sprite *s, int x, int y, float scale, float angle)
{
    struct sprite_pos pos;
    struct sprite_xform xf;

    pos.x = x << 8;
    pos.y = y << 8;
    xf.scale = scale;
    xf.angle = angle;
    sprite_draw(s, &pos, &xf);
}

/* 0x446240: dst = table[src << 8 | dst] over the rectangle
 * (x0, y0)..(x1, y1) of src, put at (dx, dy) of dst. */
void blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                const u8 *table, int src_pitch, int dst_pitch)
{
    int r, x;

    for (r = 0; r < y1 - y0; r++) {
        const u8 *s = src + (y0 + r) * src_pitch;
        u8 *d = dst + (dy + r) * dst_pitch + dx - x0;

        for (x = x0; x < x1; x++) {
            d[x] = table[s[x] << 8 | d[x]];
        }
    }
}

static const char *mini_text(u32 addr)
{
    return ui_text(addr + 200 * g_menu_language);
}

static void draw_centred(const char *text, int font, int y)
{
    text_draw(text, font, 160 - text_width(text, font) / 2, y);
}

/* The blended panel layer under the current page (0x40a5e6). */
static void blend_panels(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int w = mode[0];
    struct menu_page *cur = g_menu_view.page;
    const u8 *tab = g_menu_table;
    const struct menu_visual *v;

    if (cur == page(PAGE_BEST_TIMES)) {
        blend_rect(g_menu_buffer2, 8, 0, 0x13f, 0xb4, g_menu_screen, 8, 0, tab, w, w);
    } else if (cur == page(PAGE_NETWORK)) {
        blend_rect(g_menu_buffer2, 0x0a, 0, 0x13f, 0xc7, g_menu_screen, 0x0a, 0, tab, w, w);
    } else if (cur == page(22) || cur == page(23) || cur == page(24)) {
        blend_rect(g_menu_buffer2, 0, 0x40, 0x13f, 0x9f, g_menu_screen, 0, 0x40, tab, w, w);
    } else if (cur == page(17) || cur == page(18)) {
        blend_rect(g_menu_buffer2, 0, 0x41, 0x140, 0xa8, g_menu_screen, 0, 0x41, tab, w, w);
    } else if (cur == page(19) || cur == page(20)) {
        blend_rect(g_menu_buffer2, 0, 0x41, 0x140, 0xbc, g_menu_screen, 0, 0x41, tab, w, w);
    } else if ((v = cur->items[0].visuals) != NULL
               && (fe_4be850 == 0.0 || fe_4be850 >= 1306.8)) {
        int y = v->y + mode[1] / 2 - 4;

        blend_rect(g_menu_buffer2, 0, y, 0x13f, y + cur->count * 19 + 5, g_menu_screen,
                   0, y, tab, w, w);
    }
}

/* Player keys move the car selection on page 5; elsewhere the key map
 * is restored (0x40a003). */
static void player_keys(void)
{
    static const s8 actions[4] = { MENU_KEY_DEC_WRAP, MENU_KEY_INC_WRAP,
                                   MENU_KEY_UP_WRAP, MENU_KEY_DOWN_WRAP };
    int i;

    if (g_menu_view.page == page(PAGE_CAR_P2)) {
        for (i = 0; i < 4; i++) {
            u8 key = g_settings[0x187 + i];

            if (g_menu_keymap[key] == 0) {
                g_menu_keymap[key] = actions[i];
            }
        }
    } else {
        for (i = 1; i <= 4; i++) {
            u8 key = g_settings[0x186 + i];

            g_menu_keymap[key] = g_menu_keymap_orig[key];
        }
    }
}

/* The cars of the selection, from `first` on, wrapped (0x40ab5a). */
static void car_ring(int player, int first, int n, s32 *cars)
{
    int k;

    for (k = 0; k < n; k++) {
        s32 idx = settings_get(0x40 + 4 * player) + k + first;

        if (idx < 0) {
            idx += g_car_count;
        }
        if (g_car_count <= idx) {
            idx -= g_car_count;
        }
        cars[k] = g_car_order[idx];
    }
}

/* The car carousel under the selected car (0x40ab3e): three cars at
 * rest, four while it turns. The cars are 3D (car3d_draw). */
static void car_carousel(int player)
{
    double spin = menu_dt() * 27.777777777777775;
    s32 cars[4];

    if (fe_4be87c > 0) {
        double s = sin(fe_4be87c * 0.00392699081698725);

        if (fe_4be5fc < 0) {
            car_ring(player, -1, 4, cars);
            car3d_draw(cars[0], (int)(spin * 1.0588235294117647), s + 0.01, 0x4e, 0x9e);
            car3d_draw(cars[3], (int)(spin * 1.0588235294117647), 1.01 - s, 0xf0, 0x9e);
            car3d_draw(cars[1], (int)(spin * 1.0588235294117647), s * 0.6000000000000001 + 1.01,
                       (int)(s * 81.0 + 78.0), 0x9e);
            car3d_draw(cars[2], (int)(spin * 1.894736842105263), s * -0.6000000000000001 + 1.61,
                       (int)(s * 81.0 + 159.0), 0x9e);
        } else {
            car_ring(player, -2, 4, cars);
            car3d_draw(cars[3], (int)(spin * 1.0588235294117647), s + 0.01, 0xf0, 0x9e);
            car3d_draw(cars[2], (int)(spin * 1.0588235294117647), s * 0.6000000000000001 + 1.01,
                       (int)(s * -81.0 + 240.0), 0x9e);
            car3d_draw(cars[0], (int)(spin * 1.0588235294117647), 1.01 - s, 0x4e, 0x9e);
            car3d_draw(cars[1], (int)(spin * 1.894736842105263), s * -0.6000000000000001 + 1.61,
                       (int)(s * -81.0 + 159.0), 0x9e);
        }
    } else {
        car_ring(player, -1, 3, cars);
        car3d_draw(cars[0], (int)(spin * 1.0588235294117647), 1.01, 0x4e, 0x9e);
        car3d_draw(cars[2], (int)(spin * 1.0588235294117647), 1.01, 0xf0, 0x9e);
        car3d_draw(cars[1], (int)(spin * 1.894736842105263), 1.61, 0x9f, 0x9e);
    }
}

/* How far a picture has grown back after a change: 1 - t * rate,
 * folded and capped at 1 (0x40a90e). */
static float grow(s32 t, double rate)
{
    double v = t * rate + 1.0;

    if (v < 0.0) {
        v = -v;
    }
    return (float)(1.0 < v ? 1.0 : v);
}

/* Remakes the sprites that show the current choice when its page opens
 * or the choice changes. */
static void make_choice_sprites(void)
{
    struct menu_page *cur = g_menu_view.page;
    int changed = g_menu_view.page_changed;
    int i;

    if (changed || fe_4bf554) {
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            s32 car;

            fe_4bf554 = 0;
            car = g_car_order[*cur->items[cur->selected].value];
            g_menu_desc.width = g_menu_desc.stride = 0xba;
            g_menu_desc.height = 0x3d;
            g_menu_desc.hot_x = 0x5d00;
            g_menu_desc.hot_y = 0x1e80;
            g_menu_desc.pixels = g_pic_cars + car * (0xba * 0x3d);
            g_sprite_car = sprite_create(&g_menu_desc, g_sprite_car);
        }
    }
    if ((changed && cur == page(PAGE_TRACK)) || fe_4be728) {
        struct menu_page *tracks = page(PAGE_TRACK);
        int locked = 2 - page(PAGE_DIFFICULTY)->items[0].max;

        tracks->items[6].kind = 1;
        tracks->items[7].kind = 1;
        for (i = 0; i < locked; i++) {
            tracks->items[7 - i].kind = 0;
        }
        fe_4be728 = 0;
        g_menu_desc.width = g_menu_desc.stride = 0x73;
        g_menu_desc.height = 0x63;
        g_menu_desc.hot_x = 0x3980;
        g_menu_desc.hot_y = 0x3980;
        g_menu_desc.pixels = g_pic_tracks + g_track_pic_offsets[tracks->selected];
        g_sprite_track = sprite_create(&g_menu_desc, g_sprite_track);
    }
    if ((changed && cur == page(PAGE_LANGUAGE)) || fe_4be6d8) {
        fe_4be6d8 = 0;
        g_menu_desc.width = g_menu_desc.stride = 0x3d;
        g_menu_desc.height = 0x35;
        g_menu_desc.hot_x = 0;
        g_menu_desc.hot_y = 0;
        for (i = 0; i < 5; i++) {
            /* The chosen language's flag is the lit one, six flags on. */
            int offset = i * 0xca1 + (i == settings_get(SET_LANGUAGE) ? 0x4bc6 : 0);

            g_menu_desc.pixels = g_pic_flags + offset;
            g_sprite_flags[i] = sprite_create(&g_menu_desc, g_sprite_flags[i]);
        }
    }
}

/* 0x409fb0 */
int menu_page_handler_a(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    struct menu_page *cur;
    int intro_done = fe_4be850 == 0.0 || fe_4be850 >= 1306.8;
    s16 panel;
    int i;

    /* Backspace goes back, except while typing. */
    g_menu_keymap[0x0e] = g_menu_view.edit == 0 ? MENU_KEY_BACK : 0;
    if (g_settings[0x6c] == 0) {
        g_settings[0x6c] = ' ';
        g_settings[0x6d] = 0;
    }
    if (g_settings[0x7b] == 0) {
        g_settings[0x7b] = ' ';
        g_settings[0x7c] = 0;
    }
    player_keys();
    cur = g_menu_view.page;
    if (cur != page(22) && cur != page(23) && cur != page(24)) {
        page(22)->selected = 1;
        page(23)->selected = 1;
        page(24)->selected = 1;
    }
    if (g_cd_track_count > 0) {
        struct menu_item *track = &page(PAGE_CD)->items[2];

        track->max = g_cd_track_count > 25 ? 25 : g_cd_track_count;
        if (track->max < settings_get(SET_CD_TRACK)) {
            g_menu.key_pressed[0xcd] = 1;
        }
    }
    g_joysticks[0].enabled = settings_get(0x123) > 0;
    g_joysticks[1].enabled = settings_get(0x127) > 0;
    make_choice_sprites();

    if (fe_4be58c) {
        fe_4be58c = 0;
        menu_keys_page();
        if (cur == page(PAGE_BEST_TIMES) && fe_4be598 < 1080.0) {
            menu_best_times();
        }
    }
    if (fe_4be850 == 0.0 || fe_4be850 >= 1278.0) {
        if (cur == page(PAGE_LANGUAGE)) {
            memset(g_menu_screen, 0, 64000);
        } else {
            s_background_t += menu_dt();
            if (s_background_t >= 2.1176470588235294) {
                while (s_background_t >= 2.1176470588235294) {
                    s_background_t -= 2.1176470588235294;
                }
                menu_background_render();
            }
            memcpy(g_menu_screen, g_menu_buffer1, 64000);
        }
    }
    if (cur != page(PAGE_LANGUAGE) && cur != page(13) && cur != page(14)
        && cur != page(PAGE_BEST_TIMES) && cur != page(PAGE_NETWORK) && intro_done) {
        draw_sprite_at(g_sprite_logo, 0x4b, 2);
    }
    panel = idle_seq((int)(fe_4be598 - 1080.0) / 0xa8);
    if (panel == -2) {
        draw_sprite_at(g_sprite_panel, 0xa0, 0x7d);
    } else if (panel == -3) {
        draw_sprite_at(g_sprite_panel2, 0xa0, 0x7d);
    }
    if (cur == page(PAGE_BEST_TIMES) && g_menu_view.page_changed) {
        menu_best_times();
    }
    if (g_menu_sound) {
        sound_tick();
    }
    if (fe_4be598 > 1080.0) {
        int w = mode[0];

        blend_rect(g_menu_buffer2, 0x0a, 0x44, 0x13f, (s32)g_frame_time % 0x329 < 500 ? 0xc7 : 0xbf,
                   g_menu_screen, 0x0a, 0x44, g_menu_table, w, w);
    } else {
        blend_panels();
    }

    if (fe_4be598 < 1080.0) {
        if (cur == page(PAGE_LANGUAGE)) {
            /* Four flags: the second language has none here. */
            for (i = 0; i < 5; i++) {
                if (i != 1) {
                    draw_sprite_at(g_sprite_flags[i], (i > 1 ? i - 1 : i) * 69 + 0x1a, 0x4a);
                }
            }
            draw_centred("SELECT YOUR LANGUAGE", g_fonts_menu[6], 0xbf);
        }
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            float scale = grow(fe_4be87c, -0.00625);

            if (fe_4be87c == 0) {
                draw_sprite_at(g_sprite_car, 0xa0, 0x5a);
            } else {
                draw_sprite_scaled(g_sprite_car, 0xa0, 0x5a, scale, 0.0f);
            }
            draw_sprite_at(g_sprite_car_select, 0x20, 0x7a);
            if (page(0)->selected == 1 && page(2)->selected == 0) {
                draw_centred(mini_text(cur == page(PAGE_CAR_P1) ? 0x47dffc : 0x47e000),
                             g_fonts_menu[6], 0xc0);
            }
        }
        if (cur == page(PAGE_TRACK)) {
            float scale = grow(fe_4be724, -0.005);

            if (fe_4be724 == 0) {
                draw_sprite_at(g_sprite_track, 0xfb, 0x88);
            } else {
                draw_sprite_scaled(g_sprite_track, 0xfb, 0x88, scale, 0.0f);
            }
            if (g_menu_view.page_changed) {
                if (page(0)->selected == 1 && page(2)->selected == 0) {
                    fe_4be588 = 1;
                } else {
                    fe_4be594 = 1;
                }
            }
        }
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            int player = cur == page(PAGE_CAR_P1) ? 0 : 1;

            if (player == 1 && g_menu_view.page_changed) {
                fe_4be588 = 1;
            }
            car_carousel(player);
        }
    }
    if (cur == page(PAGE_NETWORK)) {
        menu_network_page();
    } else {
        fe_4be710 = 2;
        fe_4be568 = 0;
    }
    if (intro_done) {
        const s32 *r = g_position_icon_rect;

        draw_sprite_at(g_position_icons[0], 0, 0);
        draw_sprite_at(g_position_icons[1], mode[0] - r[2], 0);
        draw_sprite_at(g_position_icons[2], 0, mode[1] - r[3]);
        draw_sprite_at(g_position_icons[3], mode[0] - r[2], mode[1] - r[3]);
    }
    return 0;
}
