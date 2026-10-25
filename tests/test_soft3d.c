/*
 * test_soft3d.c - draws random commands of every type through the
 * software rasteriser (0x44f0e9), some far off the screen, and checks
 * that nothing is written outside the clip rectangle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "soft3d/soft3d.h"
#include "soft3d/sw.h"

enum {
    W = 400, H = 300,           /* the screen */
    X0 = 37, Y0 = 21, X1 = 330, Y1 = 250,    /* the clip, ends included */
    COUNT = 4000
};

static u8 s_screen[W * H];
static u8 *s_pages;             /* two 64 KB pages */
static u8 *s_table;             /* 64 KB aligned */

static u32 s_seed = 12345;

static s32 rnd(s32 lo, s32 hi)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return lo + (s32)((s_seed >> 8) % (u32)(hi - lo + 1));
}

/* A position in 8.8: mostly on the screen, sometimes far off it. */
static s32 pos(int far, int size)
{
    if (far) {
        return rnd(-40000, 40000) * 256 + rnd(0, 255);
    }
    return rnd(-60, size + 60) * 256 + rnd(0, 255);
}

int main(void)
{
    static union {
        s32 type;
        struct sw_cmd_point point;
        struct sw_cmd_point_table point_table;
        struct sw_cmd_line line;
        struct sw_cmd_line_shade shade;
        struct sw_cmd_flat flat;
        struct sw_cmd_flat16 flat16;
        struct sw_cmd_texture tex;
        struct sw_cmd_texture_table tex_table;
        struct sw_cmd_flat_table flat_table;
        struct sw_cmd_persp persp;
        struct sw_cmd_sprite sprite;
    } cmd;
    static const int types[] = { 1, 2, 7, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21 };
    static s32 uv[6];
    void *list[2] = { &cmd, NULL };
    struct sw_raster ctx;
    u8 *raw;
    int i, k, x, y, bad = 0;

    raw = malloc(5 * 0x10000);
    if (raw == NULL) {
        return 1;
    }
    s_pages = (u8 *)(((uintptr_t)raw + 0xffff) & ~(uintptr_t)0xffff);
    s_table = s_pages + 2 * 0x10000;
    for (i = 0; i < 2 * 0x10000; i++) {
        s_pages[i] = (u8)(1 + i % 251);
    }
    for (i = 0; i < 0x10000; i++) {
        s_table[i] = (u8)(1 + (i >> 8) % 254);
    }
    if (sw_raster_set_screen(W, H) != 0) {
        return 1;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.cmds = list;
    ctx.target = s_screen;
    ctx.target2 = s_screen;
    ctx.table = s_table;
    ctx.x0 = X0;
    ctx.y0 = Y0;
    ctx.x1 = X1;
    ctx.y1 = Y1;
    for (i = 0; i < COUNT; i++) {
        int type = types[i % (int)(sizeof(types) / sizeof(types[0]))];
        int far = rnd(0, 9) == 0;

        memset(&cmd, 0, sizeof(cmd));
        memset(s_screen, 0, sizeof(s_screen));
        cmd.type = type;
        for (k = 0; k < 6; k++) {
            uv[k] = rnd(0, 255) * 256 + rnd(0, 255);
        }
        switch (type) {
        case 1:
            cmd.point.x = pos(far, W);
            cmd.point.y = pos(far, H);
            cmd.point.colour = rnd(1, 255);
            break;
        case 2:
            cmd.point_table.x = pos(far, W);
            cmd.point_table.y = pos(far, H);
            cmd.point_table.colour = rnd(1, 255);
            cmd.point_table.table = s_table;
            break;
        case 7:
            cmd.sprite.src = &cmd.sprite.src_;
            cmd.sprite.scale = &cmd.sprite.scale_;
            cmd.sprite.x = pos(far, W);
            cmd.sprite.y = pos(far, H);
            cmd.sprite.src_.u0 = rnd(0, 100) * 256;
            cmd.sprite.src_.v0 = rnd(0, 100) * 256;
            cmd.sprite.src_.u1 = cmd.sprite.src_.u0 + rnd(1, 120) * 256;
            cmd.sprite.src_.v1 = cmd.sprite.src_.v0 + rnd(1, 120) * 256;
            cmd.sprite.src_.hot_x = (cmd.sprite.src_.u1 - cmd.sprite.src_.u0) / 2;
            cmd.sprite.src_.hot_y = (cmd.sprite.src_.v1 - cmd.sprite.src_.v0) / 2;
            cmd.sprite.src_.tex = s_pages;
            cmd.sprite.src_.table = s_table;
            cmd.sprite.scale_.sx = rnd(16, 2048);
            cmd.sprite.scale_.sy = rnd(16, 2048);
            break;
        case 11:
        case 12:
            cmd.line.x0 = pos(far, W);
            cmd.line.y0 = pos(far, H);
            cmd.line.x1 = pos(far, W);
            cmd.line.y1 = pos(far, H);
            cmd.line.colour = rnd(1, 255);
            break;
        case 13:
            cmd.shade.x0 = pos(far, W);
            cmd.shade.y0 = pos(far, H);
            cmd.shade.x1 = pos(far, W);
            cmd.shade.y1 = pos(far, H);
            cmd.shade.table = s_table;
            cmd.shade.c0 = rnd(1, 255) * 256;
            cmd.shade.c1 = rnd(1, 255) * 256;
            break;
        case 15:
            for (k = 0; k < 3; k++) {
                cmd.flat.v[k].x = pos(far, W);
                cmd.flat.v[k].y = pos(far, H);
            }
            cmd.flat.colour = rnd(1, 255);
            break;
        case 16:
            for (k = 0; k < 3; k++) {
                cmd.flat16.v[k].x = pos(far, W);
                cmd.flat16.v[k].y = pos(far, H);
                cmd.flat16.v[k].i = rnd(0, 255) * 256;
            }
            cmd.flat16.colour = rnd(1, 255);
            break;
        case 17:
        case 18:
            for (k = 0; k < 3; k++) {
                cmd.tex_table.v[k].x = pos(far, W);
                cmd.tex_table.v[k].y = pos(far, H);
            }
            cmd.tex_table.uv = uv;
            cmd.tex_table.tex = s_pages;
            cmd.tex_table.table = s_table;
            break;
        case 19:
            for (k = 0; k < 3; k++) {
                cmd.flat_table.v[k].x = pos(far, W);
                cmd.flat_table.v[k].y = pos(far, H);
            }
            cmd.flat_table.colour = rnd(1, 255);
            cmd.flat_table.table = s_table;
            break;
        default:
            for (k = 0; k < 3; k++) {
                cmd.persp.v[k].x = pos(far, W);
                cmd.persp.v[k].y = pos(far, H);
                cmd.persp.v[k].z = rnd(176, 20000);
            }
            memcpy(cmd.persp.uv, uv, sizeof(uv));
            cmd.persp.tex = s_pages;
            cmd.persp.table = s_table;
            break;
        }
        sw_raster_draw(&ctx);
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                if (s_screen[y * W + x] != 0 && (x < X0 || x > X1 || y < Y0 || y > Y1)) {
                    if (bad < 20) {
                        fprintf(stderr, "command %d, type %d: a pixel at %d, %d\n", i, type, x, y);
                    }
                    bad++;
                    y = H;
                    break;
                }
            }
        }
    }
    puts(bad ? "FAIL" : "ok");
    free(raw);
    return bad != 0;
}
