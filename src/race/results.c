/*
 * race/results.c - the screens after the race (0x438a60): the track's
 * results, its score and the total score, page by page; the end of a
 * championship with its statue, or the offer to retry; and the two
 * players' final page.
 *
 * Each page is drawn once into the race buffer (0x563d40) when the hot
 * keys (0x420eb0) move r_5530a8 to an even stage, and the stage then
 * becomes odd. The buffer holds the page two ways at once: 320 wide by
 * 480 for the panels and the low resolution footer, and 640 wide by 240
 * for the high resolution footer at the same bytes. Each frame the
 * finished cars' rows are drawn onto the page, and the page is blended
 * onto the screen a row at a time as r_5db038 and r_54f9d0 (0x435350)
 * reveal it.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cd.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/log.h"
#include "lisa/sprite.h"
#include "menu/pages.h"
#include "menu/panels.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/contact.h"
#include "race/data.h"
#include "race/hud.h"
#include "race/init.h"
#include "race/misc.h"
#include "prof.h"
#include "race/results.h"
#include "startup.h"

extern double r_553090;                 /* the race's time in seconds */
extern s32 r_54f978, r_54f9d0, r_5db038, r_563ce0, r_54f988, r_619310, r_601664;
extern s32 r_6192a0[10], r_6192c8[10], r_553010[8];
extern double r_552f68;
extern s32 g_race_5285c8;
extern s32 r_525e44;

s32 r_563d68;                           /* RETURN goes on to the next track */
s32 r_525e70[RACE_DRIVERS];             /* the cars by total score */
s32 r_553040[RACE_DRIVERS];             /* their totals */
s32 r_552d90[6], r_552df0[6];           /* the confetti's places, never drawn */
struct sprite_xform r_563d70;           /* the statue's scale as it grows */
char r_601680[16];                      /* the ghost's driver, from its file (0x420240) */

/* The page's fonts: the panels', and the low and high resolution
 * footers'. */
#define F_PANEL (g_fonts_menu[1])       /* 0x552fd4 */
#define F_LOW (g_fonts_menu[6])         /* 0x552fe8 */
#define F_HIGH (g_fonts_menu[7])        /* 0x552fec */

enum { PAGE_BYTES = 0x25800 };

/* The race's language (0x639933). The original reads its tables at any
 * index; here one out of range reads the first. */
static int lang(void)
{
    s32 l = race_cfg(0x593);

    return l >= 0 && l < 6 ? l : 0;
}

static void cfg_set(int offset, s32 v)
{
    memcpy(g_race_cfg + offset, &v, sizeof(v));
}

static double cfg_double(int offset)
{
    double d;

    memcpy(&d, g_race_cfg + offset, sizeof(d));
    return d;
}

/* The sprites after 0x5286f4 run on into 0x52870c's; the rows' place
 * numbers index them as one. */
static struct sprite *place_sprite(int k)
{
    return k < 6 ? g_race_5286f4[k] : g_race_52870c[k - 6];
}

/* The statues, three by difficulty, gold first (0x5287b8..). */
static struct sprite *statue(int k)
{
    static struct sprite **const sets[4] = { g_race_5287b8, g_race_5287c4, g_race_5287d0,
                                             g_race_5287dc };

    return sets[(k / 3) & 3][k % 3];
}

static void draw_at(const struct sprite *s, s32 x, s32 y, const struct sprite_xform *xf)
{
    struct sprite_pos pos;

    pos.x = x;
    pos.y = y;
    sprite_draw(s, &pos, xf);
}

static void text_centred(const char *text, int font, int x, int y)
{
    g_fonts[font].align = FONT_ALIGN_CENTER;
    text_draw(text, font, x, y);
    g_fonts[font].align = FONT_ALIGN_LEFT;
}

static void target_page(void)
{
    set_target(g_race_buffer, 0x140, 0x140, 0x1e0, 8);
}

static void target_screen(void)
{
    set_target(g_back_buffer, g_screen_width, g_screen_width, g_screen_height, 8);
    g_gfx_color_table = g_level_pan;
}

/* NATIVE's results at the menus' size: each pixel of the page 2 wide and
 * 2.4 tall, as the menus' 320 x 200 is shown at 4:3. */
#define SX 2
#define SY 2.4

/* A length down the page, on the screen. */
static int down(int n)
{
    return race_box_scaled() ? (int)(n * SY + 0.5) : n;
}

/* The page's rectangle (x0, y0)..(x1, y1) onto the screen at y; dx is
 * where the original puts it across, about the screen's middle. */
static void put(int x0, int y0, int x1, int y1, int pitch, int dx, int y)
{
    int cx = g_screen_width / 2;

    if (race_box_scaled()) {
        blend_rect_scaled(g_race_buffer, x0, y0, x1, y1, g_back_buffer, cx + (dx - cx) * SX, y,
                          (x1 - x0) * SX, down(y1 - y0), g_level_pan, pitch, g_screen_width,
                          g_screen_height);
    } else {
        blend_rect(g_race_buffer, x0, y0, x1, y1, g_back_buffer, dx, y, g_level_pan, pitch,
                   g_screen_width);
    }
}

static void panel(int x, int width, int y)
{
    panel_add(x, 0, width, y, 0);
    panels_draw(g_race_buffer, 0x140);
    panels_clear();
}

/* A page cleared, with its title on a panel at the top. */
static void page_title(const char *title)
{
    int w = text_width(title, F_PANEL);

    memset(g_race_buffer, 0, PAGE_BYTES);
    target_page();
    g_gfx_color_table = g_identity_table;
    panel_add(0x93 - w / 2, 0, w, 1, 0);
    panels_draw(g_race_buffer, 0x140);
    panels_clear();
    text_centred(title, F_PANEL, 0xa0, 0x0d);
}

/* The footer, both ways: on the 320 wide page and the 640 wide one. */
static void page_footer(const char *text)
{
    text_centred(text, F_LOW, 0xa0, 0x19c);
    set_target(g_race_buffer, 0x280, 0x280, 0xf0, 8);
    text_centred(text, F_HIGH, 0x140, 0xd3);
    target_page();
}

/* The car at `place` (1 based), or g_driver_count for none. */
static int car_at(int place)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        if (g_cars[i].f3a0 == place) {
            break;
        }
    }
    return i;
}

/* The track results' row frames (0x438f19, 0x43c75a). */
static void result_frames(void)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        s32 y = 0x28 + 0x3c * i, sy = 0x3000 + 0x3c00 * i;

        if (g_race_mode == 3) {
            draw_at(place_sprite(i), 0x7000, sy, NULL);
            panel(0x8b, 0x28, y);
            continue;
        }
        draw_at(place_sprite(i), 0x3400, sy, NULL);
        panel(0x4f, 0x28, y);
        panel(0x92, 0x5f, y);
        if (r_553068 == 1) {
            draw_at(place_sprite(i + 1), 0x3400, sy + 0x3c00, NULL);
            panel(0x4f, 0x28, y + 0x3c);
            panel(0x92, 0x5f, y + 0x3c);
        }
    }
}

/* The score pages' row frames. */
static void score_frames(void)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        s32 y = 0x28 + 0x3c * i;

        draw_at(place_sprite(i), 0x5600, 0x3000 + 0x3c00 * i, NULL);
        panel(0x73, 0x28, y);
        panel(0xb7, 0x16, y);
    }
}

/* The track results' footer by the network mode and race mode. */
static void result_footer(void)
{
    int l = lang();

    if (g_race_net.mode == 4) {
        page_footer(g_res_waiting[l]);
    } else if (g_race_mode != 0) {
        page_footer(g_res_play_again[l]);
        g_race_5285c8 = 1;
    } else {
        page_footer(g_res_press_return[l]);
    }
}

/* 0x43c3c0: the track results page's footer and frames again, for the
 * network game. */
static void result_page_again(void)
{
    target_page();
    g_gfx_color_table = g_identity_table;
    result_footer();
    result_frames();
    target_screen();
}

/* The points a car has: the championship's and this race's. */
static s32 total_points(int i)
{
    return g_place_points[g_cars[i].f3a0] + g_champ_points[i];
}

/* The totals and the cars ordered by them, best first, into r_553040
 * and r_525e70. */
static void sort_totals(void)
{
    int i, j, k;

    for (i = 0; i < g_driver_count; i++) {
        r_525e70[i] = i;
        r_553040[i] = total_points(i);
    }
    for (j = g_driver_count; j >= 0; j--) {
        for (k = 1; k < g_driver_count; k++) {
            if (r_553040[k - 1] < r_553040[k]) {
                s32 t = r_553040[k];

                r_553040[k] = r_553040[k - 1];
                r_553040[k - 1] = t;
                t = r_525e70[k];
                r_525e70[k] = r_525e70[k - 1];
                r_525e70[k - 1] = t;
            }
        }
    }
}

/* Whether a player made the top three. The original tests this once a
 * car, and reads the second car even with one. */
static int players_placed(void)
{
    int k;

    for (k = 0; k < g_driver_count; k++) {
        if (g_cars[0].f3a0 < 4) {
            return 1;
        }
        if (g_driver_count > 1 && g_cars[1].f3a0 < 4 && g_two_players == 1) {
            return 1;
        }
    }
    return 0;
}

/* The championship's last track at this difficulty (0x6393ac is the
 * race's copy of it, cfg 0xf3 the track in the championship). */
static int last_track(void)
{
    s32 track = race_cfg(0x0f3), diff = race_cfg(0x00c);

    return (track == 4 && diff == 0) || (track == 5 && diff == 1)
        || (track == 6 && diff == 2) || (track == 6 && diff == 3);
}

/* Stage 4: the total score page. */
static void total_page(void)
{
    int l = lang();

    page_title(g_res_total_score[l]);
    if (players_placed()) {
        if (last_track()) {
            page_footer(g_res_level_done[l]);
            r_563d68 = 0;
        } else {
            page_footer(g_res_well_done[l]);
            r_563d68 = 1;
            r_619310 = g_track + 1;
        }
    } else {
        char buf[100];

        snprintf(buf, sizeof(buf), "%s", g_res_press_return[l]);
        page_footer(buf);
        r_563d68 = 0;
        g_race_5285c8 = 0;
    }
    score_frames();
    target_screen();
}

/* Stage 6: the championship is over. The players' cars drive on their
 * own; the champion, if it is the player, opens the next difficulty. */
static void championship_end(void)
{
    int k;

    memset(g_race_buffer, 0, PAGE_BYTES);
    r_563d68 = 1;
    r_5530a8 = 7;
    if (!players_placed()) {
        return;
    }
    g_cars[0].f5d4 = 0;
    g_cars[1].f5d4 = 0;
    r_563d68 = 0;
    r_601664 = 0;
    g_view_car = 0;
    g_cars[0].f604 = 1;
    if (g_two_players == 1) {
        g_cars[1].f604 = 1;
    }
    for (k = 0; k < 6; k++) {
        r_6192a0[k] = 100;
        r_6192c8[k] = 100;
    }
    sort_totals();
    if (r_525e70[0] != 0 || g_two_players != 0) {
        return;
    }
    if (race_cfg(0x0f3) == 6 && race_cfg(0x00c) == 3) {
        cfg_set(0x58b, 1);
    }
    if (race_cfg(0x0f3) == 6 && race_cfg(0x00c) <= 2) {
        if (race_cfg(0x587) < 3) {
            cfg_set(0x587, 3);
        }
        cfg_set(0x00c, 3);
    }
    if (race_cfg(0x0f3) == 5 && race_cfg(0x00c) <= 1) {
        if (race_cfg(0x587) < 2) {
            cfg_set(0x587, 2);
        }
        cfg_set(0x00c, 2);
    }
    if (race_cfg(0x0f3) == 4 && race_cfg(0x00c) == 0) {
        if (race_cfg(0x587) < 1) {
            cfg_set(0x587, 1);
        }
        cfg_set(0x00c, 1);
    }
}

/* One row of a page, by stage, for the row's place `row` (0 based).
 * Returns the row counter to go on from: the ghost race's rows set it
 * themselves, as the original reuses it. */
static int draw_row(int row, const s32 *order, const s32 *totals)
{
    char buf[64];
    int odd, car;
    s32 x, y = row * 0x3c00 + 0x2500, ty = row * 60 + 0x34;

    if (g_race_mode == 3) {
        car = car_at(6 - row);
        if (car >= g_driver_count) {
            return row;
        }
        target_page();
        g_gfx_color_table = g_identity_table;
        x = (row & 1) ? 0x3b00 : 0xd300;
        draw_at(g_race_52870c[g_drivers[car].car], x, 0x15100 - row * 0x3c00, NULL);
        snprintf(buf, sizeof(buf), "%s", g_drivers[car].name);
        text_centred(buf, F_PANEL, 0xad, 0x160 - row * 60);
        target_screen();
        return row;
    }
    car = car_at(row + 1);
    if (car >= g_driver_count && r_553068 != 1) {
        return row;
    }
    target_page();
    g_gfx_color_table = g_identity_table;
    if (r_5530a8 == 1) {
        if (r_553068 == 0) {
            odd = row & 1;
            draw_at(g_race_52870c[g_drivers[car].car], odd ? 0x10d00 : 0x100, y, NULL);
            snprintf(buf, sizeof(buf), "%s", g_drivers[car].name);
            text_centred(buf, F_PANEL, 0x71, ty);
            snprintf(buf, sizeof(buf), "%s", race_time_text(g_cars[car].f3a4));
            text_centred(buf, F_PANEL, 0xce, ty);
        } else {
            /* Against the ghost: the faster first. */
            double ghost = r_525e44 * 0.01;

            row = ghost <= g_cars[0].f3a4 ? 1 : 0;
            odd = row & 1;
            y = row * 0x3c00 + 0x2500;
            ty = row * 60 + 0x34;
            draw_at(g_race_52870c[g_drivers[0].car], odd ? 0x10d00 : 0x100, y, NULL);
            snprintf(buf, sizeof(buf), "%s", g_drivers[0].name);
            text_centred(buf, F_PANEL, 0x71, ty);
            snprintf(buf, sizeof(buf), "%s", race_time_text(g_cars[0].f3a4));
            text_centred(buf, F_PANEL, 0xce, ty);
            row ^= 1;
            odd = row & 1;
            y = row * 0x3c00 + 0x2500;
            ty = row * 60 + 0x34;
            draw_at(g_race_52870c[11], odd ? 0x10d00 : 0x100, y, NULL);
            snprintf(buf, sizeof(buf), "%s", r_601680);
            text_centred(buf, F_PANEL, 0x71, ty);
            snprintf(buf, sizeof(buf), "%s", race_time_text(ghost));
            text_centred(buf, F_PANEL, 0xce, ty);
        }
        target_screen();
        return row;
    }
    if (r_5530a8 == 3 || r_5530a8 == 5) {
        s32 value;

        if (r_5530a8 == 5) {
            car = order[row];
        } else if (car >= g_driver_count) {
            /* The ghost race has no score pages; the original would read
             * past the cars. */
            log_printf("race_results: no car at place %d\n", row + 1);
            return row;
        }
        value = r_5530a8 == 5 ? totals[row] : g_place_points[g_cars[car].f3a0];
        odd = row & 1;
        draw_at(g_race_52870c[g_drivers[car].car], odd ? 0xeb00 : 0x2200, y, NULL);
        snprintf(buf, sizeof(buf), "%s", g_drivers[car].name);
        text_centred(buf, F_PANEL, 0x93, ty);
        snprintf(buf, sizeof(buf), "%d", value);
        text_centred(buf, F_PANEL, 0xcf, ty);
        target_screen();
        return row;
    }
    /* Other stages leave the page as the target, as the original does. */
    return row;
}

/* A name copied the way MSVC's sprintf copies "%s": forwards, a byte at
 * a time, so a copy onto bytes just before it works. The record tables'
 * names are 4 bytes apart and longer names run into the next ones. */
static void copy_name(u8 *dst, const u8 *dst_end, const u8 *src)
{
    const u8 *end = g_race_cfg + sizeof(g_race_cfg);

    while (src < end && dst < dst_end) {
        u8 c = *src++;

        *dst++ = c;
        if (c == 0) {
            break;
        }
    }
}

/* A time into a track's table at `base` (names at +4k, times at
 * +0x10 + 8k): into the last slot if better, then up past the slower
 * ones. Slots 1..5 are the ones the menu shows. */
static void insert_record(int base, double time, const char *name)
{
    u8 *names = g_race_cfg + base, *times = names + 0x10;
    int k;

    if (!(time < cfg_double(base + 0x10 + 40))) {
        return;
    }
    memcpy(times + 40, &time, sizeof(time));
    copy_name(names + 20, g_race_cfg + sizeof(g_race_cfg), (const u8 *)name);
    for (k = 4; k > 0; k--) {
        double a, b;
        u8 tmp[0x78];

        memcpy(&a, times + 8 * (k + 1), sizeof(a));
        memcpy(&b, times + 8 * k, sizeof(b));
        if (!(a < b)) {
            continue;
        }
        tmp[0] = 0;
        copy_name(tmp, tmp + sizeof(tmp) - 1, names + 4 * k);
        tmp[sizeof(tmp) - 1] = 0;
        memcpy(times + 8 * k, &a, sizeof(a));
        copy_name(names + 4 * k, g_race_cfg + sizeof(g_race_cfg), names + 4 * (k + 1));
        memcpy(times + 8 * (k + 1), &b, sizeof(b));
        copy_name(names + 4 * (k + 1), g_race_cfg + sizeof(g_race_cfg), tmp);
    }
}

/* The players' times into the track's best race and lap tables, once
 * each. */
static void store_records(void)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        const char *name = (const char *)g_race_cfg + 0x6c + 15 * i;
        int base = 0x1bb + 120 * g_track;
        const struct car *c = &g_cars[i];

        if (c->f528 != 1 || (i < 8 && r_553010[i] != 0) || g_drivers[0].control != 0) {
            continue;
        }
        if (i != 0 && !(i == 1 && g_two_players == 1)) {
            continue;
        }
        r_553010[i] = 1;
        insert_record(base, c->f3a4, name);
        insert_record(base + 0x3c, c->f3bc, name);
        insert_record(base + 0x3c, c->f3c4, name);
        insert_record(base + 0x3c, c->f3cc, name);
    }
}

/* Stages 6 and 7 for a player in the top three: the statue grows in,
 * and the championship's result. */
static void podium(void)
{
    char buf[256];
    int l = lang(), d = g_mirror, k, scaled = race_box_scaled();
    /* NATIVE draws it on the page as on a 320 x 200 screen, then the page
     * onto the screen at the menus' size, as the results' rows. */
    int sw = scaled ? 0x140 : g_screen_width, sh = scaled ? 200 : g_screen_height;
    const struct car *c = &g_cars[0];
    s32 x = (sw / 2 - 0x6c) << 8, y = (s32)(sh * 102.4);
    double h = sh;
    const struct sprite_xform *xf;
    int tx = sw / 2 - 0x44;

    /* The rows before leave the page as the target (draw_row), and the
     * podium's places are the screen's: the port draws it there. */
    if (scaled) {
        memset(g_race_buffer, 0, 0x140 * 200);
        set_target(g_race_buffer, 0x140, 0x140, 200, 8);
        g_gfx_color_table = g_identity_table;
    } else {
        target_screen();
    }
    r_563d70.angle = 0;
    r_563d70.scale = (float)(c->f5d4 * 0.04);
    if (r_563ce0 == 0) {
        cd_play_track(8);
        r_563ce0 = 1;
    }
    xf = c->f5d4 < 25 ? &r_563d70 : NULL;
    if (r_525e70[0] == 0 || (r_525e70[0] == 1 && g_two_players == 1)) {
        draw_at(statue(d * 3), x, y, xf);
    } else if (r_525e70[1] == 0 || (r_525e70[1] == 1 && g_two_players == 1)) {
        draw_at(statue(d * 3 + 1), x, y, xf);
    } else if (r_525e70[2] == 0 || (r_525e70[2] == 1 && g_two_players == 1)) {
        draw_at(statue(d * 3 + 2), x, y, xf);
    }
    snprintf(buf, sizeof(buf), "%s", g_res_congrats[l]);
    text_draw(buf, F_HIGH, tx, (s32)(h * 0.4 - 45.0));
    snprintf(buf, sizeof(buf), "%s", g_res_completed[l]);
    text_draw(buf, F_HIGH, tx, (s32)(h * 0.4 - 22.0));
    snprintf(buf, sizeof(buf), g_res_championship[l], g_res_leagues[4 * l + d]);
    text_draw(buf, F_HIGH, tx, (s32)(h * 0.4 - 5.0));
    /* With the player not in the top three the buffer keeps the line
     * before, as in the original. */
    if (r_525e70[0] == 0) {
        snprintf(buf, sizeof(buf), g_res_first[l], total_points(0));
    } else if (r_525e70[1] == 0) {
        snprintf(buf, sizeof(buf), g_res_second[l], total_points(0));
    } else if (r_525e70[2] == 0) {
        snprintf(buf, sizeof(buf), g_res_third[l], total_points(0));
    }
    text_draw(buf, F_HIGH, tx, (s32)(h * 0.4 + 15.0));
    snprintf(buf, sizeof(buf), g_res_score_of[l], total_points(0));
    text_draw(buf, F_HIGH, tx, (s32)(h * 0.4 + 35.0));
    if (g_two_players == 0) {
        if (r_525e70[0] != 0) {
            snprintf(buf, sizeof(buf), "%s", g_res_must_achieve[l]);
            text_centred(buf, F_HIGH, sw / 2, (s32)(h * 0.4 + 65.0));
            snprintf(buf, sizeof(buf), "%s", g_res_gold_statue[l]);
            text_centred(buf, F_HIGH, sw / 2, (s32)(h * 0.4 + 80.0));
        } else if (d < 3) {
            snprintf(buf, sizeof(buf), g_res_now_try[l], g_res_leagues[4 * l + d + 1]);
            text_centred(buf, F_HIGH, sw / 2, (s32)(h * 0.4 + 75.0));
        }
    }
    if (g_screen_size == 0) {
        text_centred(g_res_press_return[l], F_LOW, sw / 2, sh - 10);
    } else {
        text_centred(g_res_press_return[l], F_HIGH, sw / 2, sh - 20);
    }
    if (scaled) {
        target_screen();
        put(0, 0, 0x140, 200, 0x140, g_screen_width / 2 - 0xa0, g_screen_height / 2 - down(100));
    }
    /* Confetti about the view's middle; the original works it out but
     * never draws it. */
    for (k = 0; k < 6; k++) {
        const struct race_driver *v = &g_drivers[0];

        if (r_6192a0[k] > 12 && c->f5d4 < 20) {
            s32 x0 = v->view_x0;

            r_552d90[k] = (s32)((race_rand() * 90.0 - 45.0) * r_563d70.scale
                                + (double)((v->view_x1 - x0) / 2 + x0));
            r_552df0[k] = (s32)((race_rand() * 90.0 - 45.0) * r_563d70.scale
                                + (double)((v->view_y1 - v->view_y0) / 2));
            if (r_6192a0[k] >= 100) {
                r_6192a0[k] = (s32)(race_rand() * 12.0);
            } else {
                r_6192a0[k] = 0;
            }
        }
        if (r_6192a0[k] < 13) {
            r_563d70.angle = 0;
            r_563d70.scale = 0.5f;
        }
    }
}

/* Stages 6 and 7 for players out of the top three: in a championship
 * the offer to retry or quit, else the place sliding in. */
static void retry_menu(void)
{
    char buf[128], retry[64], quit[64];
    int l = lang(), w, k;

    if (g_race_mode <= 0 && r_563c60 == 0 && r_563da0 == 0) {
        s32 x = (g_screen_width / 2 - 0x4c) << 8;
        s32 y = (s32)((sin(r_552f68) * g_screen_height * 0.6 - 51.0) * 256.0);

        if (race_box_scaled()) {
            /* At the menus' size: drawn on the page below its 480 rows,
             * then blended up, its top sliding as the original's. */
            int top = (int)(sin(r_552f68) * g_screen_height * 0.6) - down(51);

            memset(g_race_buffer + 0x200 * 0x140, 0, 0x40 * 0x140);
            set_target(g_race_buffer, 0x140, 0x140, 0x240, 8);
            g_gfx_color_table = g_identity_table;
            draw_at(g_hud_places4[0], 0x74 << 8, 0x200 << 8, NULL);
            target_screen();
            put(0x74, 0x200, 0x74 + 0x99, 0x200 + 0x32, 0x140, g_screen_width / 2 - 0x4c, top);
            return;
        }
        target_screen();
        draw_at(g_hud_places4[0], x, y, NULL);
        return;
    }
    for (k = 0x2580; k < 0xaf00; k += 0x140) {
        memset(g_race_buffer + k, 0, 0x140);
    }
    set_target(g_race_buffer, 0x140, 0x140, g_screen_height, 8);
    g_gfx_color_table = g_identity_table;
    g_fonts[F_HIGH].align = FONT_ALIGN_CENTER;
    snprintf(buf, sizeof(buf), "%s", g_res_sorry[l]);
    text_draw(buf, F_HIGH, 0xa0, 0x28);
    snprintf(buf, sizeof(buf), "%s", g_res_or_better[l]);
    text_draw(buf, F_HIGH, 0xa0, 0x37);
    g_fonts[F_HIGH].align = FONT_ALIGN_LEFT;
    snprintf(retry, sizeof(retry), "%s", g_res_retry[l]);
    panel_add(0x32, 0, text_width(retry, F_PANEL), 0x50, 0);
    snprintf(quit, sizeof(quit), "%s", g_res_quit[l]);
    panel_add(0x32, 1, text_width(quit, F_PANEL), 0x50, 0);
    panels_draw(g_race_buffer, 0x140);
    panels_clear();
    text_draw(retry, r_54f988 == 0 ? F_PANEL : g_fonts_menu[0], 0x40, 0x5c);
    text_draw(quit, r_54f988 == 1 ? F_PANEL : g_fonts_menu[0], 0x40, 0x6f);
    /* A mark for each try left. */
    w = text_width(retry, F_PANEL);
    for (k = 0; k < g_race_mode; k++) {
        draw_at(g_race_5287e8, ((w + 0x5a) << 8) + k * 0x1900, 0x5800, NULL);
    }
    g_gfx_color_table = g_level_pan;
    set_target(g_back_buffer, g_screen_width, g_screen_width, g_screen_height, 8);
    put(0, 0x1e, 0x13f, 0x8c, 0x140, g_screen_width / 2 - 0xa0, g_screen_height / 2 - down(0x46));
}

/* The index of car `car` in the total score order, or `keep`. */
static int rank_of(int car, int keep)
{
    int k;

    for (k = 0; k < 6; k++) {
        if (r_525e70[k] == car) {
            return k;
        }
    }
    return keep;
}

/* One player's line on the two players' page. */
static void final_line(int car, int rank, s32 sy, s32 dy, s32 py, s32 ty)
{
    char buf[64];
    int w;

    if (rank < 3) {
        draw_at(statue(g_mirror * 3 + rank), 0x2800, sy << 8, &r_563d70);
    }
    draw_at(place_sprite(rank), 0x4600, dy << 8, NULL);
    panel(0x64, 0x2d, py);
    snprintf(buf, sizeof(buf), "%s", g_drivers[car].name);
    text_centred(buf, F_PANEL, 0x87, ty);
    snprintf(buf, sizeof(buf), g_res_pts[lang()], total_points(car));
    w = text_width(buf, F_PANEL);
    panel(0xaf, w, py);
    text_centred(buf, F_PANEL, w / 2 + 0xbc, ty);
}

/* Stage 8: the two players' final page, drawn each frame. */
static void final_page(void)
{
    int l = lang(), lead, other, rank;
    s32 mid = g_screen_height / 2, eighth = g_screen_height / 8, top;

    page_title(g_res_total_score[l]);
    sort_totals();
    if (total_points(1) > total_points(0)) {
        lead = 1;
        other = 0;
    } else {
        lead = 0;
        other = 1;
    }
    rank = rank_of(lead, -1);
    if (rank < 0) {
        /* The original reads a stale stack word here. */
        log_printf("race_results: player %d not in the first six\n", lead);
        rank = 5;
    }
    r_563d70.scale = g_screen_size != 0 ? 1.0f : 0.5f;
    r_563d70.angle = 0;
    top = mid - eighth;
    final_line(lead, rank, top - 13, top - 15, top - 0x17, top - 11);
    rank = rank_of(other, rank);
    r_563d70.scale = g_screen_size != 0 ? 1.0f : 0.5f;
    r_563d70.angle = 0;
    final_line(other, rank, eighth + mid + 0x19, eighth + mid + 0x0f, eighth + mid + 7,
               eighth + mid + 0x13);
    target_screen();
    blend_rect(g_race_buffer, 0, 0, 0x140, g_screen_height, g_back_buffer,
               g_screen_width / 2 - 0xa0, 0, g_level_pan, 0x140, g_screen_width);
    if (g_screen_size == 0) {
        text_centred(g_res_press_return[l], F_LOW, 0xa0, g_screen_height - 10);
    } else {
        text_centred(g_res_press_return[l], F_HIGH, g_screen_width / 2, g_screen_height - 16);
    }
}

/* The rows revealed so far onto the screen: the one being revealed up to
 * r_5db038, those before in full (in an elimination race those after,
 * as it counts down). */
static void blend_rows(int step, s32 top)
{
    int rows = g_driver_count, row;
    s32 y1 = 0x57, dy = top - 0x1a, cx = g_screen_width / 2;

    if (r_553068 == 1 && (double)(r_525e44 / 100) <= r_553090) {
        rows++;
    }
    if (race_box_scaled() && rows > 0) {
        /* Under the title, the rows as far apart as fit above the
         * footer, but no closer than a row's name bar. */
        int title = down(0x1d), foot = down(16) + 4, tall = down(0x36);
        int room = g_screen_height - foot - tall;

        dy = down(top - 0x1a);
        step = down(step);
        if (rows > 1 && dy + (rows - 1) * step > room) {
            step = (room - dy) / (rows - 1);
            if (step < down(0x18)) {
                step = down(0x18);
                dy = room - (rows - 1) * step;
                if (dy < title) {
                    dy = title;
                }
            }
        }
    }
    for (row = 0; row < rows; row++, y1 += 0x3c, dy += step) {
        int car = car_at(row + 1);
        int odd = row & 1;

        if (r_553068 == 1) {
            if (car == g_driver_count) {
                car = 0;
            }
        }
        if (car >= g_driver_count && r_553068 != 1) {
            continue;
        }
        if (!(r_5530a8 > 0 && r_5530a8 < 6 && r_54f978 == 1)) {
            continue;
        }
        if (r_54f9d0 == row) {
            put(0, y1 - 0x36, r_5db038, y1, 0x140, cx - 0xa0, dy);
            continue;
        }
        if (g_race_mode == 3) {
            if (r_54f9d0 >= row) {
                continue;
            }
            if (odd) {
                put(0xcd, y1 - 0x36, 0x109, y1, 0x140, cx + 0x2d, dy);
            } else {
                put(0x37, y1 - 0x36, 0x73, y1, 0x140, cx - 0x69, dy);
            }
            put(0x69, y1 - 0x27, 0xd7, y1 - 0x0f, 0x140, cx - 0x37, dy + down(0x0f));
            continue;
        }
        if (r_54f9d0 <= row) {
            continue;
        }
        if (r_5530a8 == 1) {
            if (odd) {
                put(0x10e, y1 - 0x36, 0x140, y1, 0x140, cx + 0x6e, dy);
            } else {
                put(0, y1 - 0x36, 0x32, y1, 0x140, cx - 0xa0, dy);
            }
            put(0x32, y1 - 0x27, 0x10e, y1 - 0x0f, 0x140, cx - 0x6e, dy + down(0x0f));
        } else if (r_5530a8 == 3 || r_5530a8 == 5) {
            if (odd) {
                put(0xed, y1 - 0x36, 0x122, y1, 0x140, cx + 0x4d, dy);
            } else {
                put(0x1e, y1 - 0x36, 0x50, y1, 0x140, cx - 0x82, dy);
            }
            put(0x50, y1 - 0x27, 0xed, y1 - 0x0f, 0x140, cx - 0x50, dy + down(0x0f));
        }
    }
}

/* 0x438a60 */
int race_results(void)
{
    s32 order[RACE_DRIVERS], totals[RACE_DRIVERS];
    int i, row, step;
    s32 top;

    if (r_527f28 != 0) {
        r_527f28 = 0;
        race_init_hud();
    }
    /* Not until every player has finished. */
    for (i = 0; i < g_driver_count; i++) {
        if (g_cars[i].f528 == 0 && g_drivers[i].control == 0) {
            return 0;
        }
    }
    for (i = 0; i < RACE_DRIVERS; i++) {
        order[i] = 100;
    }
    gfx_set_clip(0, 0, g_screen_width, g_screen_height);

    if (r_5530a8 == 0) {
        r_5530a8 = 1;
        for (i = 0; i < r_553280 && i < RACE_DRIVERS; i++) {
            int car = car_at(i + 1);

            if (car < g_driver_count) {
                order[i] = car;
            }
        }
        page_title(g_res_track_results[lang()]);
        result_footer();
        result_frames();
        target_screen();
    }
    if (r_5530a8 == 2) {
        r_5530a8 = 3;
        page_title(g_res_track_score[lang()]);
        page_footer(g_res_press_return[lang()]);
        score_frames();
        target_screen();
    }
    if (r_5530a8 == 4) {
        r_5530a8 = 5;
        total_page();
    }
    if (r_5530a8 == 6) {
        championship_end();
    }

    /* The rows of the cars that have finished. */
    if (r_5530a8 > 0 && r_553070 < r_553280) {
        for (i = 0; i < g_driver_count; i++) {
            order[i] = i;
            totals[i] = total_points(i);
        }
        if (r_5530a8 == 5) {
            int j, k;

            for (j = g_driver_count; j >= 0; j--) {
                for (k = 1; k < g_driver_count; k++) {
                    if (totals[k - 1] < totals[k]) {
                        s32 t = totals[k];

                        totals[k] = totals[k - 1];
                        totals[k - 1] = t;
                        t = order[k];
                        order[k] = order[k - 1];
                        order[k - 1] = t;
                    }
                }
            }
        }
        for (row = 0; row < g_driver_count; row++) {
            row = draw_row(row, order, totals);
        }
        r_553070 = r_553280;
    }
    store_records();

    if (g_screen_size == 0) {
        step = 25;
        top = 45;
    } else {
        step = 40;
        top = 80;
    }
    if (r_5530a8 > 0) {
        if (r_5530a8 < 6 && r_54f978 == 1) {
            int cx = g_screen_width / 2;

            put(0, 8, 0x140, 0x25, 0x140, cx - 0xa0, 0);
            if (g_screen_size == 0) {
                put(0, 0x19c, 0x140, 0x1a4, 0x140, cx - 0xa0, g_screen_height - down(8));
            } else {
                put(0, 0xd3, 0x280, 0xe3, 0x280, cx - 0x140, g_screen_height - down(16));
            }
        }
        if (r_5530a8 > 5 && r_5530a8 < 8) {
            if (g_cars[0].f3a0 < 4) {
                podium();
            } else {
                retry_menu();
            }
        }
        if (r_5530a8 == 8) {
            final_page();
        }
    }
    prof_begin(PROF_ROWS);
    blend_rows(step, top);
    prof_end(PROF_ROWS);
    if (g_race_net.mode != 2) {
        result_page_again();
    }
    /* The original points the rasteriser's list (0x498730) back at the
     * back buffer here; the port's rasteriser takes its target each
     * call. */
    return 0;
}
