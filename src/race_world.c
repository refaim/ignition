/*
 * race_world.c - the race's 3D world (0x41b470): the scene, its
 * textures, and an object for each placement of the track.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "platform.h"
#include "gl3d.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "race.h"
#include "scene.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

struct scene_desc *g_world;             /* 0x54f908 */
s32 r_563c80[0x18];                     /* 0x563c80, view sizes */
s32 r_54f9d4, r_527f2c;
s32 r_54f954, r_563cf0, r_563d78, r_552f78, r_54f9b8;

/* Whether a placement of type 100 and up is in this race: some only
 * come with r_553084 (0x41b8a6). */
static int placed(int i, int type)
{
    if (type >= 300 && type < 350 && !r_553084) {
        return 0;
    }
    if (type >= 150 && type <= 199 && !r_553084) {
        return 0;
    }
    if (type == 357 && !r_553084) {
        return 0;
    }
    if ((type == 350 || type == 351) && g_level_pos[i] != -1 && !r_553084) {
        return 0;
    }
    return 1;
}

static void create(int i, struct plc_entry *e)
{
    struct scene_desc *d = &g_world[i];

    if (scene_obj_create(d, g_level_msh + e->mesh, 0, e->type, g_road_order[i].order, -1,
                         g_plc_extra[i].f0) == -1) {
        log_printf("Error while creating the world");
        exit(1);
    }
    d->x = e->x + 0x6400;
    d->y = e->y;
    d->z = e->z + 0x6400;
    d->ax = d->ay = d->az = 0;
    scene_obj_link(d);
}

/* 0x41b470, the scenery so far: the cars, their wheels and shadows,
 * the collision objects, lens flares and the .SRF come later. */
void race_make_world(void)
{
    static const s32 sizes[0x18] = {
        1, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 0, 2, 1, 1, 1, 1, 1, 1, 0
    };
    struct scene *sc = scene_create();
    int n = g_level_plc[0], i;

    sc->perspective = r_525e68;
    sc->zoom = g_mirror >= 3 ? -1.0 : 1.0;
    for (i = 0; i < RACE_TEX_SLOTS; i++) {
        g_scene_textures[i] = g_race_tex[i];
        g_scene_texture_pages[i] = g_race_tex_pages[i];
    }
    /* 0x447280 builds blurred levels of the track's texture when
     * r_527f78 is set; GL samples the pages as they are. */
    memcpy(r_563c80, sizes, sizeof(sizes));
    r_527f2c = g_race_screen_size != 0 ? 7 : 6;
    r_54f9d4 = r_563c80[g_two_players + 2 * (r_527f28 + 4 * g_race_screen_size)];

    if (raster_set_grid(0xc800, 0xc800, 1500) == -1) {
        mem_out_of_memory();
    }
    g_world = mem_calloc(g_race_pool, (u32)n * sizeof(*g_world));
    g_race_bytes += n * (s32)sizeof(*g_world);
    if (g_world == NULL) {
        mem_out_of_memory();
    }
    r_54f954 = r_563cf0 = r_563d78 = r_552f78 = r_54f9b8 = 0;
    for (i = 0; i < n; i++) {
        struct plc_entry *e = plc_at(g_level_plc, i);

        if (e->type < 100) {
            create(i, e);
        }
    }
    for (i = 0; i < n; i++) {
        struct plc_entry *e = plc_at(g_level_plc, i);

        if (e->type >= 100 && placed(i, e->type)) {
            create(i, e);
            if (e->type >= 150 && e->type < 200) {
                r_54f954++;
            }
        }
    }
    STUB(0x41bb1f, "race_make_world: collision objects, flares, cars, srf");
}

/* An all zero screen: the back buffer's colour 0 shows the 3D. */
static const u8 s_clear[320 * 200];

/* Draws the 3D at the drawable's size, its field of view widened to
 * the drawable's shape; the back buffer is left clear for the HUD, and
 * presenting shows it over the 3D where it is not 0. */
void race_draw_view(const struct scene *sc, struct scene_obj *const *objs, int n)
{
    int w, h;
    double vw;

    memset(g_back_buffer, 0, sizeof(g_back_buffer));
    video_drawable_size(&w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    /* The original's 320 x 200 fills a 4:3 picture; a wider drawable
     * sees more at the sides. */
    vw = 320.0 * ((double)w / h) / (4.0 / 3.0);
    gl3d_view_begin(w, h, vw, 200.0);
    gl3d_draw(sc, objs, n, (vw - 320.0) / 2, 0);
    gl3d_view_end();
    video_set_layer(gl3d_view_texture(), s_clear);
}

/* Until the race's cameras are ported: the track from behind the
 * start line, drawn into the back buffer. */
void race_debug_view(void)
{
    static u32 start;
    struct scene *sc = g_scene;
    struct scene_obj *objs[1500];
    const struct plc_entry *a, *b;
    double t, f, ha, hb, dh, h, x, y, z;
    int n = 0, i, piece;

    if (sc == NULL || g_track_link_count < 2) {
        return;
    }
    /* Two pieces a second, gliding from one to the next. */
    if (start == 0) {
        start = plat_time_ms();
    }
    t = (plat_time_ms() - start) / 500.0;
    piece = (int)t % (g_track_link_count - 1);
    f = t - (int)t;
    a = plc_at(g_level_plc, g_track_links[piece].plc);
    b = plc_at(g_level_plc, g_track_links[piece + 1].plc);
    ha = g_track_links[piece].heading;
    hb = g_track_links[piece + 1].heading;
    dh = remainder(hb - ha, 2.0 * M_PI);
    h = ha + dh * f;
    x = a->x + (b->x - a->x) * f + 0x6400;
    y = a->y + (b->y - a->y) * f;
    z = a->z + (b->z - a->z) * f + 0x6400;
    sc->x = x + sin(h) * 600.0;
    sc->y = y + 300.0;
    sc->z = z - cos(h) * 600.0;
    sc->target_x = (s32)x;
    sc->target_y = (s32)y;
    sc->target_z = (s32)z;
    scene_look_at(sc);
    for (i = 0; i < g_level_plc[0] && n < 1500; i++) {
        if (g_world[i].obj != NULL) {
            objs[n++] = g_world[i].obj;
        }
    }
    race_draw_view(sc, objs, n);
    video_set_palette(g_level_col + 8);
    present_back_buffer();
}
