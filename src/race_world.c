/*
 * race_world.c - the race's 3D world (0x41b470): the scene, its
 * textures, and an object for each placement of the track.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
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

/* Until the race's cameras are ported: the track from behind the
 * start line, drawn into the back buffer. */
void race_debug_view(void)
{
    static double t;
    struct scene *sc = g_scene;
    struct scene_obj *objs[1500];
    const struct track_link *l;
    const struct plc_entry *e;
    int n = 0, i, piece;

    if (sc == NULL || g_track_link_count == 0) {
        return;
    }
    t += 0.02;
    piece = (int)t % g_track_link_count;
    l = &g_track_links[piece];
    e = plc_at(g_level_plc, l->plc);
    sc->x = e->x + 0x6400 + sin(l->heading) * 600.0;
    sc->y = e->y + 300.0;
    sc->z = e->z + 0x6400 - cos(l->heading) * 600.0;
    sc->target_x = e->x + 0x6400;
    sc->target_y = e->y;
    sc->target_z = e->z + 0x6400;
    scene_look_at(sc);
    for (i = 0; i < g_level_plc[0] && n < 1500; i++) {
        if (g_world[i].obj != NULL) {
            objs[n++] = g_world[i].obj;
        }
    }
    gl3d_begin(320, 200);
    gl3d_draw(sc, objs, n, 0, 0);
    memset(g_back_buffer, 0, sizeof(g_back_buffer));
    gl3d_end(g_back_buffer, 320);
    video_set_palette(g_level_col + 8);
    present_back_buffer();
}
