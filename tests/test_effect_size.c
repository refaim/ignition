/* Real effect mesh producers, with scene insertion replaced at its boundary. */
#include <stdio.h>
#include "settings.h"
#include "../src/race/fx.c"

u8 g_settings[SETTINGS_SIZE + 0x10];
s32 r_552d80, r_552f5c;
s32 g_part_meshes[FX_SLOTS][200];
static struct scene_desc part_objects[FX_SLOTS];
struct scene_desc *g_part_objs = part_objects;
static struct car car;
struct car *g_cars = &car;
s32 g_weather;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int scene_obj_link(struct scene_desc *d) { d->in_grid = 1; return 0; }
int scene_obj_move(struct scene_desc *d) { (void)d; return 0; }
int scene_obj_unlink(struct scene_desc *d) { d->in_grid = 0; return 0; }
int log_printf(const char *fmt, ...) { (void)fmt; return 0; }
int scene_obj_set_mesh(struct scene_desc *d, const s32 *m, int tex, int klass,
                       int flags, int radius, int group)
{ (void)d; (void)m; (void)tex; (void)klass; (void)flags; (void)radius; (void)group; return 0; }

static void producers(int choice, s32 factor_x, s32 factor_y)
{
    s32 mesh[14] = {0};
    const s32 frame[] = {1, 2, 3, 4};
    s32 weather[64];
    struct race_effect halo = {0};
    int k;

    settings_set(SET_EFFECT_SIZE, choice);
    sprite_frame(mesh, frame, 0, 96);
    CHECK(mesh[12] == 3 * (factor_x / 32));
    CHECK(mesh[13] == 3 * (factor_y / 32));
    CHECK(mesh[7] == 256 && mesh[10] == 1024);
    sprite_frame(mesh, NULL, 0, 96);
    CHECK(mesh[7] == 256 && mesh[10] == 1024);
    CHECK(mesh[12] == 3 * (factor_x / 32));
    halo.f24 = 100;
    halo.f14 = 1024;
    halo.f18 = 2048;
    halo.f1c = 3072;
    fx_halo(&halo, 0);
    CHECK(g_part_meshes[0][12] == factor_x * 150);
    CHECK(g_part_meshes[0][13] == factor_y * 150);
    CHECK(g_part_meshes[0][7] == 0x2100 && g_part_meshes[0][10] == 0x7500);
    CHECK(g_part_meshes[0][2] == 250);
    CHECK(halo.f20 == 1 && halo.y == 1024 && halo.z == 2048 && halo.f10 == 3072);
    for (k = 0; k < 64; k++) weather[k] = 12345;
    g_weather = 2;
    weather_faces(weather, 1);
    for (k = 0; k < 2; k++) {
        const s32 *f = weather + 8 + 9 * k;
        CHECK(f[0] == 7 && f[1] == k);
        CHECK(f[3] == 0x7500 && f[5] == 0x9400);
        CHECK(f[7] == factor_x * 100 && f[8] == factor_y * 100);
    }
    CHECK(weather[0] == 12345 && weather[7] == 12345 && weather[26] == 12345);
    /* Rain lines never used these factors: preserve their geometry/colour. */
    g_weather = 1;
    weather_faces(weather, 1);
    CHECK(weather[14] == 13 && weather[15] == 0 && weather[16] == 1);
    CHECK(weather[17] == 0x7700 && weather[18] == 0x7b00);
}

int main(void)
{
    r_552d80 = 1057;
    r_552f5c = 1121;
    CHECK(settings_get(SET_EFFECT_SIZE) == EFFECT_SIZE_SMALL);
    producers(EFFECT_SIZE_SMALL, 420, 448);
    producers(EFFECT_SIZE_ORIGINAL, 1057, 1121);
    producers(EFFECT_SIZE_SMALL, 420, 448);
    puts(failures ? "FAIL" : "ok");
    return failures != 0;
}
