/*
 * soft3d/mip.c - MIP MAPPING (0x447280): a texture slot's blurred
 * levels, for faces far from the eye, and its magnified tiles, for
 * faces near it.
 *
 * A level is the texture blurred at its own size, tile by tile (64 x 64
 * texels): the first a 2 x 2 box (0x447fb0), the next the four texels
 * one further out (0x4481f0). The magnified tiles (0x4475c0) are each
 * tile blown up 4 times, texels between filled in linearly. Colours are
 * mixed in RGB, the error carried to the next pixel, and taken back to
 * the palette through a 64 x 64 x 64 table of the nearest colour
 * (0x448620).
 */

#include <stdlib.h>
#include <string.h>

#include "race/race.h"
#include "soft3d/soft3d.h"
#include "soft3d/sw.h"

const struct sw_tex_handle *g_sw_tex_handles[SCENE_TEXTURES];

/* The nearest palette colour of each RGB in 6 bits a channel,
 * r << 12 | g << 6 | b (0x4cdd18). */
static u8 r_4cdd18[0x40000];

/* What 0x447280 allocated, freed with the scene (0x63c5d0). */
enum { BLOCKS = 64 };
static void *s_blocks[BLOCKS];
static int s_block_count;

static void *block(size_t size)
{
    void *p;

    if (s_block_count == BLOCKS) {
        return NULL;
    }
    p = calloc(1, size);
    if (p != NULL) {
        s_blocks[s_block_count++] = p;
    }
    return p;
}

/* 0x446c30's share: the scene frees what 0x447280 made, and leaves
 * the frame counter of 0x4466d0 set. */
void soft3d_scene_freed(void)
{
    int i;

    for (i = 0; i < s_block_count; i++) {
        free(s_blocks[i]);
    }
    s_block_count = 0;
    memset(g_sw_tex_handles, 0, sizeof(g_sw_tex_handles));
    r_49c9bc = 1;
    g_sw_mip = 0;
}

/* 0x448620
 * The nearest colour table for a palette of 3 bytes a colour. The
 * original keeps it in .\levels\<sum>conv.tab and reads it from there
 * when it can; the port makes it each time. */
static void make_lookup(const u8 *pal)
{
    s32 i, best = 0;

    for (i = 0; i < 0x40000; i++) {
        s32 r = (i & 0x3f000) >> 10, g = (i & 0xfc0) >> 4, b = (i & 0x3f) << 2;
        s32 most = 0x7f000000, c;

        for (c = 0; c < 256; c++) {
            s32 d = (r - pal[3 * c]) * (r - pal[3 * c]);

            if (d >= most) {
                continue;
            }
            d += (g - pal[3 * c + 1]) * (g - pal[3 * c + 1]);
            if (d >= most) {
                continue;
            }
            d += (b - pal[3 * c + 2]) * (b - pal[3 * c + 2]);
            if (d >= most) {
                continue;
            }
            most = d;
            best = c;
        }
        r_4cdd18[i] = (u8)best;
    }
}

/* A sum of four colours (10 bits a channel) and the error carried into
 * it, to the nearest palette colour; the error left is half the
 * difference. */
static u8 to_palette(const s32 sum[3], s32 err[3], const u8 *pal4)
{
    s32 s[3];
    u8 idx;
    int c;

    for (c = 0; c < 3; c++) {
        s[c] = err[c] + sum[c] + 8;
        if (s[c] > 0x3ff) {
            s[c] = 0x3ff;
        }
        if (s[c] < 0) {
            s[c] = 0;
        }
    }
    idx = r_4cdd18[((s[2] & 0x3f0) >> 4) + ((s[0] & 0x3f0) << 8) + (s[1] & 0x3f0) * 4];
    for (c = 0; c < 3; c++) {
        err[c] = (s[c] - pal4[idx * 4 + c] * 4) / 2;
    }
    return idx;
}

/* 0x447fb0
 * A w x h tile blurred by a 2 x 2 box: each texel with the ones right,
 * below and right below, the tile's last column and row taking
 * themselves again. */
static void blur_box(const u8 *src, u8 *dst, s32 w, s32 h, s32 pitch, const u8 *pal4)
{
    s32 x, y, c, sum[3], err[3];

    for (y = 0; y < h; y++) {
        err[0] = err[1] = err[2] = 0;
        for (x = 0; x < w; x++, src++, dst++) {
            for (c = 0; c < 3; c++) {
                s32 a = pal4[src[0] * 4 + c], s;

                s = x < w - 1 ? a + pal4[src[1] * 4 + c] : a * 2;
                if (y < h - 1) {
                    s32 b = pal4[src[pitch] * 4 + c];

                    s += b;
                    s += x < w - 1 ? pal4[src[pitch + 1] * 4 + c] : b;
                } else {
                    s += a;
                    s += x < w - 1 ? pal4[src[1] * 4 + c] : a;
                }
                sum[c] = s;
            }
            *dst = to_palette(sum, err, pal4);
        }
        src += pitch - w;
        dst += pitch - w;
    }
}

/* 0x4481f0
 * A w x h tile blurred by the four texels d away diagonally, kept
 * within the tile. Past the last row, the original takes the right one
 * from the first row. */
static void blur_wide(const u8 *src, u8 *dst, s32 w, s32 h, s32 pitch, const u8 *pal4, s32 d)
{
    s32 x, y, c, sum[3], err[3];

    for (y = 0; y < h; y++, dst += pitch) {
        const u8 *up = src + (y - d) * pitch, *down = src + (y + d) * pitch;
        const u8 *last = src + (h - 1) * pitch;

        err[0] = err[1] = err[2] = 0;
        for (x = 0; x < w; x++) {
            u8 tl, tr, bl, br;

            if (y - d > 0) {
                tl = x - d > 0 ? up[x - d] : up[0];
                tr = x + d < w ? up[x + d] : up[w - 1];
            } else {
                tl = x - d > 0 ? src[x - d] : src[0];
                tr = w > x + d ? src[x + d] : src[w - 1];
            }
            if (h > y + d) {
                bl = x - d > 0 ? down[x - d] : down[0];
                br = x + d < w ? down[x + d] : down[w - 1];
            } else {
                bl = x - d > 0 ? last[x - d] : last[0];
                br = w > x + d ? src[x + d] : last[w - 1];
            }
            for (c = 0; c < 3; c++) {
                sum[c] = pal4[tl * 4 + c] + pal4[tr * 4 + c] + pal4[bl * 4 + c]
                         + pal4[br * 4 + c];
            }
            dst[x] = to_palette(sum, err, pal4);
        }
    }
}

/* A magnified texel block in the cache: four texels' indices, and the
 * 4 x 4 block they make. */
struct mag_entry {
    u32 key;
    u8 out[16];
};

/* 0x4475c0
 * The texture's tiles magnified 4 times, a buffer for each column of
 * 64 texel wide tiles: each texel becomes a 4 x 4 block running
 * linearly to its neighbours right and below (itself at the tile's
 * right edge and the texture's last row). Blocks of the same four
 * texels are made once (a table of 65536 by a hash of them, whose
 * empty entries match four zeros). */
static void magnify(const u8 *src, const u8 *pal4, s32 rows, struct sw_tex_handle *h)
{
    struct mag_entry *cache = calloc(0x10000, sizeof(*cache));
    s32 col, x, y;

    if (cache == NULL) {
        return;
    }
    for (col = 0; col < 4; col++) {
        u8 *buf = block((size_t)rows << 10);
        int k;

        if (buf == NULL) {
            break;
        }
        for (k = 0; k < 4; k++) {
            h->slot[4 * col + k] = buf;
        }
        for (y = 0; y < rows; y++) {
            for (x = 0; x < 64; x++) {
                const u8 *p = src + y * 256 + col * 64 + x;
                u8 p00 = p[0], p01 = x < 63 ? p[1] : p00, p10, p11;
                u32 key, hash;
                struct mag_entry *e;
                u8 *out;

                if (y < rows - 1) {
                    p10 = p[256];
                    p11 = x < 63 ? p[257] : p10;
                } else {
                    p10 = p00;
                    p11 = x < 63 ? p[1] : p10;
                }
                key = (u32)p11 << 24 | (u32)p10 << 16 | (u32)p01 << 8 | p00;
                hash = ((key >> 17) + p10 + p01 + key) & 0xffff;
                e = &cache[hash];
                if (e->key != key) {
                    s32 err[3] = { 0, 0, 0 }, j;

                    e->key = key;
                    for (j = 0; j < 4; j++) {
                        s32 l[3], r[3], sum[3], c;

                        for (c = 0; c < 3; c++) {
                            l[c] = (pal4[p00 * 4 + c] * (4 - j) + pal4[p10 * 4 + c] * j) / 4;
                            r[c] = (pal4[p01 * 4 + c] * (4 - j) + pal4[p11 * 4 + c] * j) / 4;
                        }
                        for (c = 0; c < 3; c++) {
                            sum[c] = l[c] * 4;
                        }
                        e->out[4 * j] = to_palette(sum, err, pal4);
                        for (c = 0; c < 3; c++) {
                            sum[c] = l[c] * 3 + r[c];
                        }
                        e->out[4 * j + 1] = to_palette(sum, err, pal4);
                        for (c = 0; c < 3; c++) {
                            sum[c] = (r[c] + l[c]) * 2;
                        }
                        e->out[4 * j + 2] = to_palette(sum, err, pal4);
                        for (c = 0; c < 3; c++) {
                            sum[c] = r[c] * 3 + l[c];
                        }
                        e->out[4 * j + 3] = to_palette(sum, err, pal4);
                    }
                }
                out = buf + y * 0x400 + x * 4;
                for (k = 0; k < 4; k++) {
                    memcpy(out + k * 0x100, e->out + 4 * k, 4);
                }
            }
        }
    }
    free(cache);
}

/* 0x447280
 * Makes slot `slot`'s handle: `levels` - 1 levels, each the last
 * blurred, tile by tile (w x h texels, `rows` rows of 256), the rest of
 * the four the last made; then, with more than one level, the
 * magnified tiles. `count` and a8 only count toward a figure the
 * original does not use. */
void sw_tex_make(int slot, s32 count, s32 a8, s32 levels, s32 w, s32 h, s32 rows, const u8 *pal)
{
    u8 pal4[0x400];
    struct sw_tex_handle *hd;
    const u8 *prev;
    s32 lv, ty, tx;
    int i;

    (void)count;
    (void)a8;
    if (slot < 0 || slot >= SCENE_TEXTURES || g_scene_textures[slot] == NULL) {
        return;
    }
    for (i = 0; i < 256; i++) {
        pal4[4 * i] = pal[3 * i];
        pal4[4 * i + 1] = pal[3 * i + 1];
        pal4[4 * i + 2] = pal[3 * i + 2];
        pal4[4 * i + 3] = 0;
    }
    hd = block(sizeof(*hd));
    if (hd == NULL) {
        return;
    }
    prev = g_scene_textures[slot];
    for (i = 0; i < 5; i++) {
        hd->level[i] = prev;
    }
    hd->slot[0] = prev;
    for (lv = 0; lv < levels - 1; lv++) {
        u8 *buf = block((size_t)rows << 8);

        r_49c9bc = 1;
        g_sw_mip = 1;
        if (buf == NULL) {
            break;
        }
        if (lv + 1 < 5) {
            hd->level[lv + 1] = buf;
        }
        for (ty = 0; ty < rows / h; ty++) {
            for (tx = 0; tx < 256 / w; tx++) {
                size_t at = (size_t)ty * h * 256 + (size_t)tx * w;

                if (lv == 0) {
                    blur_box(prev + at, buf + at, w, h, 0x100, pal4);
                } else {
                    blur_wide(prev + at, buf + at, w, h, 0x100, pal4, lv);
                }
            }
        }
        prev = buf;
    }
    for (i = lv + 1; i < 5; i++) {
        hd->level[i] = prev;
    }
    g_sw_tex_handles[slot] = hd;
    if (g_sw_mip_b == 0 && levels > 1) {
        magnify(hd->level[0], pal4, rows, hd);
    }
}

/* 0x41b470's share: the race's texture slots' handles; with MIP
 * MAPPING (r_527f78 1) the track's has levels, after the nearest
 * colour table is made for the level's palette. */
void soft3d_race_textures(void)
{
    const u8 *pal = g_level_col + 8;
    int i = 0;

    if (r_527f78 == 1) {
        make_lookup(pal);
        sw_tex_make(0, 800, 0, 3, 0x40, 0x40, g_race_tex_pages[0] * 256, pal);
        i = 1;
    } else if (r_527f78 != 0) {
        return;
    }
    for (; i < 9; i++) {
        sw_tex_make(i, 500, 0, 0, 0x40, 0x40, 100, pal);
    }
}
