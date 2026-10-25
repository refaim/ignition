/*
 * soft3d/port.c - the port's glue to the software 3D: the race's views
 * and the front end's car and background drawn as the original draws
 * them, into the 8 bit buffers, under --soft3d.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/frontend.h"
#include "gl3d.h"
#include "race/init.h"
#include "race/race.h"
#include "soft3d/soft3d.h"
#include "soft3d/sw.h"
#include "startup.h"
#include "video.h"

/* The screen the rasteriser was last set to (0x44f070). */
static int s_screen_w, s_screen_h;

static int screen(int w, int h)
{
    if (w != s_screen_w || h != s_screen_h) {
        if (sw_raster_set_screen(w, h) != 0) {
            return 0;
        }
        s_screen_w = w;
        s_screen_h = h;
    }
    return 1;
}

/* The race's view (0x436990's 3D): the scene into the back buffer within
 * the view's rectangle, the .TAB for its points. The second view of a
 * split screen is driver 1's. */
void soft3d_race_view(int first)
{
    const struct race_driver *d = &g_drivers[first ? g_view_car : 1];

    video_set_layer(0, NULL);
    if (!screen(g_screen_width, g_screen_height)) {
        return;
    }
    soft3d_set_tables(g_level_shd, g_colour_key, g_level_pan);
    soft3d_draw(g_back_buffer, g_screen_width, g_screen_height, d->view_x0, d->view_y0,
                d->view_x1 - 1, d->view_y1 - 1, g_level_tab);
}

/* The rows the original fills before the 3D (the sky, track 2's band). */
void soft3d_race_fill(int y0, int y1, int index)
{
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > g_screen_height) {
        y1 = g_screen_height;
    }
    if (y1 > y0) {
        memset(g_back_buffer + (size_t)y0 * g_screen_width, index,
               (size_t)(y1 - y0) * g_screen_width);
    }
}

/* 0x40c5a0's drawing: the car's view, w x h, put with its corner at
 * (ox, oy) on the menu screen (the original moves the target and clips
 * to the view; the port also keeps it within the screen). */
void soft3d_menu_car(int ox, int oy, int w, int h)
{
    int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;

    if (!screen(w, h)) {
        return;
    }
    if (ox + x0 < 0) {
        x0 = -ox;
    }
    if (oy + y0 < 0) {
        y0 = -oy;
    }
    if (ox + x1 >= w) {
        x1 = w - 1 - ox;
    }
    if (oy + y1 >= h) {
        y1 = h - 1 - oy;
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    soft3d_draw(g_menu_screen + oy * w + ox, w, h, x0, y0, x1, y1, g_menu_table);
}

/* gl3d_raster's commands (the front end's background, the HUD's
 * triangles) as the original's, drawn by 0x44f0e9: pixels and texels
 * become 8.8, a page its 64 KB in `pages`. */
void soft3d_raster(u8 *target, int w, int h, int pitch, const struct raster_cmd *cmds, int count,
                   const u8 *pages)
{
    static union cmd {
        s32 type;
        struct sw_cmd_flat flat;
        struct sw_cmd_texture tex;
        struct sw_cmd_texture_table tex_table;
        struct sw_cmd_flat_table flat_table;
    } *s_cmds;
    static s32 *s_uv;
    static void **s_list;
    static int s_cap;
    /* The flat colours come looked up: the point table is identity. */
    static u8 s_ident[0x10000];
    struct sw_raster ctx;
    int i, k;

    if (s_ident[0xff00] == 0) {
        for (i = 0; i < 0x10000; i++) {
            s_ident[i] = (u8)(i >> 8);
        }
    }
    if (!screen(pitch, h)) {
        return;
    }
    if (count + 1 > s_cap) {
        s_cap = (count + 1) * 2;
        free(s_cmds);
        free(s_uv);
        free(s_list);
        s_cmds = malloc((size_t)s_cap * sizeof(*s_cmds));
        s_uv = malloc((size_t)s_cap * 6 * sizeof(*s_uv));
        s_list = malloc((size_t)s_cap * sizeof(*s_list));
        if (s_cmds == NULL || s_uv == NULL || s_list == NULL) {
            s_cap = 0;
            return;
        }
    }
    for (i = 0; i < count; i++) {
        const struct raster_cmd *c = &cmds[i];
        union cmd *o = &s_cmds[i];
        s32 *uv = s_uv + 6 * i;
        s32 x[3], y[3];
        const u8 *tex = pages != NULL ? pages + (size_t)c->page * 0x10000 : NULL;

        for (k = 0; k < 3; k++) {
            x[k] = (s32)lrintf(c->x[k] * 256.0f);
            y[k] = (s32)lrintf(c->y[k] * 256.0f);
            uv[2 * k] = (s32)lrintf(c->u[k] * 256.0f);
            uv[2 * k + 1] = (s32)lrintf(c->v[k] * 256.0f);
        }
        switch (c->type) {
        case RASTER_FLAT:
            o->flat.type = SW_FLAT;
            for (k = 0; k < 3; k++) {
                o->flat.v[k].x = x[k];
                o->flat.v[k].y = y[k];
                o->flat.v[k].z = 0;
            }
            o->flat.colour = c->colour;
            o->flat.f2c = 0;
            break;
        case RASTER_FLAT_TABLE:
            o->flat_table.type = SW_FLAT_TABLE;
            for (k = 0; k < 3; k++) {
                o->flat_table.v[k].x = x[k];
                o->flat_table.v[k].y = y[k];
            }
            o->flat_table.colour = c->colour;
            o->flat_table.table = c->table;
            break;
        case RASTER_TEXTURE:
        case RASTER_TEXTURE_TABLE:
            if (tex == NULL || (c->type == RASTER_TEXTURE_TABLE && c->table == NULL)) {
                o->type = -1;
                break;
            }
            o->tex_table.type = c->type == RASTER_TEXTURE ? SW_TEXTURE : SW_TEXTURE_TABLE;
            for (k = 0; k < 3; k++) {
                o->tex_table.v[k].x = x[k];
                o->tex_table.v[k].y = y[k];
            }
            o->tex_table.uv = uv;
            o->tex_table.tex = tex;
            o->tex_table.table = c->table;
            break;
        default:
            o->type = -1;
            break;
        }
    }
    for (i = k = 0; i < count; i++) {
        if (s_cmds[i].type != -1) {
            s_list[k++] = &s_cmds[i];
        }
    }
    s_list[k] = NULL;
    ctx.cmds = s_list;
    ctx.target = target;
    ctx.target2 = target;
    ctx.table = s_ident;
    ctx.x0 = 0;
    ctx.y0 = 0;
    ctx.x1 = w - 1;
    ctx.y1 = h - 1;
    sw_raster_draw(&ctx);
}
