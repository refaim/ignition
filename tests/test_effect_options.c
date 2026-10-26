/* Port persistence and generated GFX pages through the real frontend/menu engine.
 * File, video, sound and sprite boundaries are in-memory host substitutes. */
#include <stdio.h>
#include "dik.h"
#include "../src/frontend/frontend.c"

static int failures, writes, stored_size = -1, hardware = 1, resolution = CANVAS_NATIVE;
static int fail_read = -1;
static u8 stored[16];
u32 g_frame_time;
u8 g_key_pressed[256], g_key_trigger[256], g_key_down[256];
int g_soft3d;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static s32 stored_word(int index)
{
    s32 value;
    memcpy(&value, stored + index * sizeof(value), sizeof(value));
    return value;
}

int file_exists(const char *name) { (void)name; return stored_size < 0 ? FILE_ERR_NOT_FOUND : 1; }
long file_size(const char *name) { (void)name; return stored_size; }
int file_read_at(const char *name, void *out, u32 count, s32 offset)
{
    int available = stored_size - offset;
    (void)name;
    if (available < 0) available = 0;
    if (available > (int)count) available = (int)count;
    if (available) memcpy(out, stored + offset, (size_t)available);
    return available == (int)count && offset != fail_read ? 1 : FILE_ERR_READ;
}
int file_write(const char *name, const void *data, u32 count)
{
    CHECK(strcmp(name, "ign_port.cfg") == 0);
    CHECK(count <= sizeof(stored));
    memcpy(stored, data, count);
    stored_size = (int)count;
    writes++;
    return 1;
}
int canvas_hardware(void) { return hardware; }
void canvas_set_hardware(int on) { hardware = on; }
int canvas_resolution(void) { return resolution; }
void canvas_set_resolution(int mode) { resolution = mode; }
void canvas_extent(const u8 *key, float *x, float *y) { (void)key; *x = *y = 0; }
void cd_set_music_volume(int volume) { (void)volume; }
void sound_tick(void) {}
void plat_window_size(int w, int h, int *old_w, int *old_h) { (void)w; (void)h; *old_w = 640; *old_h = 480; }
int video_blit(const u8 *p, int pitch, int x, int y, int w, int h, int dx, int dy)
{ (void)p; (void)pitch; (void)x; (void)y; (void)w; (void)h; (void)dx; (void)dy; return 1; }
int video_flip(void) { return 1; }
void video_drawable_size(int *w, int *h) { *w = 320; *h = 200; }
struct sprite *sprite_create(const struct tex_desc *d, struct sprite *s) { (void)d; return s; }
int menu_pages_touch(float x, float y, int *count) { (void)x; (void)y; (void)count; return -1; }
int menu_page_handler_a(void) { return 0; }
int menu_page_handler_b(int a, int b) { (void)a; (void)b; return 0; }

static void load_words(int bytes, s32 renderer, s32 music, s32 effect)
{
    s32 words[] = {renderer, music, effect};

    memcpy(stored, words, sizeof(words));
    stored_size = bytes;
    hardware = 1;
    g_soft3d = 0;
    port_settings_load(CANVAS_NATIVE);
}

static void persistence(void)
{
    int bytes;

    for (bytes = -1; bytes <= 12; bytes++) {
        load_words(bytes, 1, 6, 1);
        CHECK(settings_get(SET_RENDERER) == (bytes >= 4 ? 1 : 0));
        CHECK(settings_get(SET_MUSIC_VOLUME) == (bytes >= 8 ? 6 : 10));
        CHECK(settings_get(0x5a0) == (bytes >= 12 ? 1 : 0));
    }
    load_words(12, -1, -1, -1);
    CHECK(settings_get(SET_RENDERER) == 1);
    CHECK(settings_get(SET_MUSIC_VOLUME) == 0);
    CHECK(settings_get(0x5a0) == 0);
    load_words(12, 3, 11, 2);
    CHECK(settings_get(SET_RENDERER) == 1);
    CHECK(settings_get(SET_MUSIC_VOLUME) == 10);
    CHECK(settings_get(0x5a0) == 0);
    load_words(12, 2, 6, 0);
    CHECK(g_soft3d == 1);
    hardware = 0;
    g_soft3d = 0;
    memset(stored, 0, sizeof(s32));
    port_settings_load(CANVAS_NATIVE);
    CHECK(settings_get(SET_RENDERER) == 1);
    hardware = 1;
    g_soft3d = 1;
    port_settings_load(CANVAS_NATIVE);
    CHECK(settings_get(SET_RENDERER) == 2);
    stored_size = -1;
    hardware = 1;
    g_soft3d = 0;
    port_settings_load(CANVAS_320X200);
    CHECK(settings_get(SET_RENDERER) == 1);
    fail_read = 8;
    load_words(12, 1, 6, 1);
    CHECK(settings_get(0x5a0) == 0);
    fail_read = -1;
    load_words(12, 1, 6, 1);
    port_settings_save();
    CHECK(stored_size == 12);
    settings_set(0x5a0, 0);
    hardware = 1;
    port_settings_load(CANVAS_NATIVE);
    CHECK(settings_get(0x5a0) == 1);
    settings_set(0x5a0, 0);
    port_settings_save();
    settings_set(0x5a0, 1);
    port_settings_load(CANVAS_NATIVE);
    CHECK(settings_get(0x5a0) == 0);
}

static void press(int key)
{
    g_key_pressed[key] = g_key_trigger[key] = 1;
    menu_engine_frame();
}

static void menu_option(void)
{
    int lang, at, k, v, before;

    add_port_options();
    add_port_options(); /* Static storage must not insert a duplicate. */
    for (lang = 0; lang < MENU_LANGUAGES; lang++) {
        struct menu_page *p = page_with(lang, 0x5a0, &at);
        int found = 0;

        CHECK(p != NULL);
        if (!p) continue;
        for (k = 0; k < p->count; k++) {
            struct menu_item *it = &p->items[k];
            if (it->value == (s32 *)(g_settings + 0x5a0)) found++;
            if (!it->value || !it->visuals) continue;
            for (v = 0; v < 2 + 2 * (it->max - it->min + 1); v++) {
                CHECK(it->visuals[v].text_y + 100 >= 0);
                CHECK(it->visuals[v].text_y + 100 + 16 <= 200);
            }
        }
        CHECK(found == 1);
        CHECK(strcmp(p->items[at].visuals[0].text, "EFFECT SIZE") == 0);
        CHECK(strcmp(p->items[at].visuals[2].text, "SMALL") == 0);
        CHECK(strcmp(p->items[at].visuals[4].text, "ORIGINAL") == 0);
        p->handler_a = NULL;
        p->handler_b = NULL;
        p->selected = 0;
        memset(&g_menu, 0, sizeof(g_menu));
        g_menu.screen_w = 320;
        g_menu.screen_h = 200;
        g_menu.centre = 1;
        g_menu.root = p;
        g_menu.key_pressed = g_key_pressed;
        g_menu.pages = g_menu_pages[lang];
        g_menu.bindable = g_menu_bindable;
        g_menu.tree = g_menu_tree;
        g_menu.tree_count = MENU_TREE_ENTRIES;
        CHECK(menu_engine_init() == 1);
        for (k = 0; k < at; k++) press(DIK_DOWN);
        CHECK(g_menu_view.selected == at);
        settings_set(0x5a0, 0);
        press(DIK_RIGHT);
        CHECK(settings_get(0x5a0) == 1);
        press(DIK_LEFT);
        CHECK(settings_get(0x5a0) == 0);
        {
            int count, key;
            float x = (p->items[at].visuals[2].x + 100 + 160) / 320.0f;
            float y = (p->items[at].visuals[0].text_y + 100 + 5) / 200.0f;
            key = menu_touch(x, y, &count);
            CHECK(key == DIK_RIGHT && count == 1);
            press(key);
            CHECK(settings_get(0x5a0) == 1);
        }
        press(DIK_ESCAPE);
        menu_engine_free();
    }
    load_words(12, 0, 7, 0);
    settings_set(SET_RESOLUTION, CANVAS_NATIVE);
    before = writes;
    menu_present();
    menu_present();
    CHECK(writes == before);
    settings_set(0x5a0, 1);
    menu_present();
    CHECK(writes == before + 1);
    CHECK(stored_size == 12 && stored_word(2) == 1);
    menu_present();
    CHECK(writes == before + 1);
    settings_set(0x5a0, 0);
    menu_present();
    CHECK(writes == before + 2);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--guards") == 0) {
        static struct menu_item full[16];
        struct menu_page *p;
        int at;

        p = page_with(0, SET_RESOLUTION, &at);
        p->items[at].value = NULL;
        p = page_with(1, SET_RESOLUTION, &at);
        memcpy(full, p->items, (size_t)p->count * sizeof(*full));
        p->items = full;
        p->count = 15;
        add_port_options();
        return 0;
    }
    persistence();
    menu_option();
    puts(failures ? "FAIL" : "ok");
    return failures != 0;
}
