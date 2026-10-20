/*
 * menubg.c - the front end's animated background (0x40c740, 0x40cd60,
 * 0x40ce00, 0x40ffe0..0x410930).
 *
 * TEST.PFM is a list of chunks, { u32 tag, s32 size, size bytes }:
 *   "li01"  a frame's vertices, s16 x, y in pixels with 4 bits of
 *           fraction; it comes before the frame's
 *   "li00"  drawing script: records of { u8 type, u8 count, count
 *           commands } up to a type of 0xff
 *   "li02"  16 bytes a textured face: u16 u0, v0, u1, v1, u2, v2 in 8.8
 *           texels, s32 page
 *   "li03"  16 bytes a sprite: s16 hot x, y; u16 u0, v0, u1, v1 in
 *           texels; u16 page; u16 shade table
 *   "lixx"  the end
 * The frames were drawn from the game's 3D and are only drawn here.
 */

#include <stdlib.h>
#include <string.h>

#include "frontend.h"
#include "frontend_state.h"
#include "gl3d.h"
#include "lisa/file.h"
#include "menubg.h"

enum { BG_PAGES = 30, BG_SHADES = 2 };

struct bg_frame {
    const u8 *script;           /* li00 */
    const u8 *verts;            /* li01 */
    s32 count;                  /* vertices */
};

static const char *const s_page_files[] = {    /* 0x4921a8 */
    "baltazar\\data\\TEXTURES\\CANADA.TEX",
    "baltazar\\data\\TEXTURES\\CARS.TEX",
    "baltazar\\data\\TEXTURES\\DIVSPR.TEX",
    "baltazar\\data\\TEXTURES\\LIGHT.TEX",
    "baltazar\\data\\TEXTURES\\SMOKE.TEX",
    "baltazar\\data\\TEXTURES\\DARKSMOK.TEX",
    "baltazar\\data\\TEXTURES\\BOOM.TEX",
    "baltazar\\data\\TEXTURES\\EXSMOKE.TEX",
};

static int s_page_count;                        /* dword_4be660's */
static const u8 *s_shades[BG_SHADES + 1];       /* 0x4be860, 0x4c4c68 */
static struct bg_frame *s_frames;               /* 0x4c4c50 */
static int s_frame_count;
static const u8 *s_uvs;                         /* li02, 0x4c4c64 */
static int s_uv_count;
static const u8 *s_sprites;                     /* li03, 0x4c4c58 */
static int s_sprite_count;
static struct raster_cmd *s_cmds;               /* 0x4c4cac */
static int s_cmd_cap;
static float *s_verts;                          /* 0x4c4c60 */
static int s_vert_cap;

static s16 rd16(const u8 *p)
{
    return (s16)(p[0] | p[1] << 8);
}

static u16 rdu16(const u8 *p)
{
    return (u16)(p[0] | p[1] << 8);
}

static s32 rd32(const u8 *p)
{
    return (s32)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
}

/* 0x40cd60: the textures, each at most 1 MB, into consecutive pages. */
int menubg_load_pages(u8 *buffer)
{
    size_t i;

    s_page_count = 0;
    for (i = 0; i < sizeof(s_page_files) / sizeof(s_page_files[0]); i++) {
        long size = file_size(s_page_files[i]);
        int pages;

        if (size <= 0) {
            return -1;
        }
        if (size > 0x100000) {
            size = 0x100000;
        }
        pages = (int)((size + 0xffff) >> 16);
        if (s_page_count + pages > BG_PAGES) {
            /* The original's buffer holds 30 pages and is not checked. */
            pages = BG_PAGES - s_page_count;
            size = (long)pages << 16;
        }
        file_read_at(s_page_files[i], buffer + ((size_t)s_page_count << 16), (u32)size, 0);
        s_page_count += pages;
    }
    gl3d_raster_pages(buffer, s_page_count);
    return 0;
}

/* 0x40ce00: CANADA.SHD, and a table that leaves colour 0 out. */
int menubg_load_shades(u8 *buffer)
{
    u8 *key = buffer + 0x10000;
    int s, d;

    file_read_at("levels\\canada\\canada.SHD", buffer, 0x10000, 0);
    for (d = 0; d < 256; d++) {
        key[d] = (u8)d;
    }
    for (s = 1; s < 256; s++) {
        memset(key + s * 256, s, 256);
    }
    s_shades[0] = buffer;
    s_shades[1] = key;
    s_shades[2] = NULL;
    return 0;
}

/* 0x40ffe0, with the chunk handlers 0x4101f0..0x410330 and the frame
 * index 0x410170. */
int menubg_parse(const u8 *pfm, long size)
{
    const u8 *p = pfm, *end = pfm + size, *verts = NULL;
    s32 vert_count = 0;
    int cap = 0, most_cmds = 0, most_verts = 0;

    menubg_free();
    while (p + 8 <= end && memcmp(p, "lixx", 4) != 0) {
        s32 len = rd32(p + 4);

        if (memcmp(p, "li01", 4) == 0) {
            verts = p + 8;
            vert_count = len / 4;
            if (vert_count > most_verts) {
                most_verts = vert_count;
            }
        } else if (memcmp(p, "li00", 4) == 0) {
            if (s_frame_count == cap) {
                cap = cap ? cap * 2 : 1024;
                s_frames = realloc(s_frames, (size_t)cap * sizeof(*s_frames));
            }
            s_frames[s_frame_count].script = p + 8;
            s_frames[s_frame_count].verts = verts;
            s_frames[s_frame_count].count = vert_count;
            s_frame_count++;
            if (len * 2 > most_cmds) {
                most_cmds = len * 2;   /* each two triangles at most */
            }
        } else if (memcmp(p, "li02", 4) == 0) {
            s_uvs = p + 8;
            s_uv_count = len / 16;
        } else if (memcmp(p, "li03", 4) == 0) {
            s_sprites = p + 8;
            s_sprite_count = len / 16;
        } else {
            return -1;
        }
        p += 8 + len;
    }
    s_cmd_cap = most_cmds;
    s_cmds = malloc((size_t)s_cmd_cap * sizeof(*s_cmds));
    s_vert_cap = most_verts;
    s_verts = malloc((size_t)(s_vert_cap * 2 + 2) * sizeof(*s_verts));
    return s_cmds != NULL && s_verts != NULL ? 0 : -1;
}

void menubg_free(void)
{
    free(s_frames);
    free(s_cmds);
    free(s_verts);
    s_frames = NULL;
    s_cmds = NULL;
    s_verts = NULL;
    s_frame_count = 0;
    s_uvs = NULL;
    s_sprites = NULL;
}

static int page_index(s32 page)
{
    if (page > 30 || page < 0) {
        page = 0;
    }
    return page < s_page_count ? page : 0;
}

static const u8 *shade(int i)
{
    return i >= 0 && i < BG_SHADES ? s_shades[i] : s_shades[0];
}

static void vertex(struct raster_cmd *c, int k, int v)
{
    c->x[k] = s_verts[2 * v];
    c->y[k] = s_verts[2 * v + 1];
}

static void textured(struct raster_cmd *c, const u8 *rec)
{
    int uv = rd16(rec + 6), k;
    const u8 *r;

    for (k = 0; k < 3; k++) {
        vertex(c, k, rd16(rec + 2 * k));
    }
    if (uv < 0 || uv >= s_uv_count) {
        uv = 0;
    }
    r = s_uvs + 16 * uv;
    for (k = 0; k < 3; k++) {
        c->u[k] = rdu16(r + 4 * k) / 256.0f;
        c->v[k] = rdu16(r + 4 * k + 2) / 256.0f;
    }
    c->page = page_index(rd32(r + 12));
}

/* A sprite as the rasteriser splits it (0x455648): a box of the
 * texture's rectangle scaled by sx, sy / 256 around its hot spot. */
static int sprite(struct raster_cmd *c, const u8 *rec)
{
    int spr = rd16(rec), v = rd16(rec + 6);
    float sx = rdu16(rec + 2) / 256.0f, sy = rdu16(rec + 4) / 256.0f;
    const u8 *d;
    float x0, y0, u0, v0, u1, v1, w, h;
    int k;

    if (spr < 0 || spr >= s_sprite_count) {
        return 0;
    }
    d = s_sprites + 16 * spr;
    u0 = rdu16(d + 4);
    v0 = rdu16(d + 6);
    u1 = rdu16(d + 8);
    v1 = rdu16(d + 10);
    x0 = s_verts[2 * v] - rd16(d) * sx;
    y0 = s_verts[2 * v + 1] - rd16(d + 2) * sy;
    w = (u1 - u0) * sx;
    h = (v1 - v0) * sy;
    for (k = 0; k < 2; k++) {
        c[k].type = RASTER_TEXTURE_TABLE;
        c[k].page = page_index(rdu16(d + 12));
        c[k].table = shade(rdu16(d + 14));
        c[k].colour = 0;
    }
    /* (P0, P1, P3) and (P1, P3, P2) */
    c[0].x[0] = x0;     c[0].y[0] = y0;     c[0].u[0] = u0; c[0].v[0] = v0;
    c[0].x[1] = x0 + w; c[0].y[1] = y0;     c[0].u[1] = u1; c[0].v[1] = v0;
    c[0].x[2] = x0;     c[0].y[2] = y0 + h; c[0].u[2] = u0; c[0].v[2] = v1;
    c[1].x[0] = x0 + w; c[1].y[0] = y0;     c[1].u[0] = u1; c[1].v[0] = v0;
    c[1].x[1] = x0;     c[1].y[1] = y0 + h; c[1].u[1] = u0; c[1].v[1] = v1;
    c[1].x[2] = x0 + w; c[1].y[2] = y0 + h; c[1].u[2] = u1; c[1].v[2] = v1;
    return 2;
}

/* 0x410440: a frame's commands. Types the script does not know end the
 * frame with nothing drawn, as the original returns no list. */
static int build_frame(int frame)
{
    const struct bg_frame *f = &s_frames[frame];
    const u8 *s = f->script;
    struct raster_cmd *c = s_cmds;
    int i, n = 0;

    for (i = 0; i < f->count * 2; i++) {
        s_verts[i] = (float)rd16(f->verts + 2 * i) / 16.0f;
    }
    while (*s != 0xff) {
        int type = s[0], count = s[1];

        s += 2;
        for (; count > 0; count--) {
            if (n + 2 > s_cmd_cap) {
                return n;
            }
            switch (type) {
            case 7:
                n += sprite(&c[n], s);
                s += 8;
                break;
            case 11:            /* a line; TEST.PFM has none */
                s += 5;
                break;
            case 13:
                s += 7;
                break;
            case 15:
                c[n].type = RASTER_FLAT;
                for (i = 0; i < 3; i++) {
                    vertex(&c[n], i, rd16(s + 2 * i));
                }
                /* TAB[colour << 8 | 0] */
                c[n].colour = g_menu_table[s[6] << 8];
                c[n].table = NULL;
                n++;
                s += 7;
                break;
            case 17:
                c[n].type = RASTER_TEXTURE;
                c[n].table = NULL;
                textured(&c[n], s);
                n++;
                s += 8;
                break;
            case 18:
                c[n].type = RASTER_TEXTURE_TABLE;
                textured(&c[n], s);
                c[n].table = shade(s[8]);
                n++;
                s += 9;
                break;
            case 19:
                c[n].type = RASTER_FLAT_TABLE;
                for (i = 0; i < 3; i++) {
                    vertex(&c[n], i, rd16(s + 2 * i));
                }
                c[n].colour = s[6];
                c[n].table = shade(s[7]);
                n++;
                s += 8;
                break;
            default:
                return 0;
            }
        }
    }
    return n;
}

/* 0x40c740: the next frame of the sequence into g_menu_buffer1. */
int menu_background_render(void)
{
    const s32 *psq = fe_4be630;
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int frame, n;

    menu_sound_tick();
    if (psq == NULL || s_frames == NULL) {
        return 0;
    }
    frame = psq[fe_4be5dc * 3] + fe_4be75c;
    fe_4be75c++;
    if (psq[fe_4be5dc * 3 + 1] == fe_4be75c) {
        fe_4be75c = 0;
        fe_4be5dc++;
        if (fe_4be5dc == fe_4be630_size / 12) {
            fe_4be5dc = 0;
        }
    }
    if (frame < 0 || frame >= s_frame_count) {
        return 0;
    }
    n = build_frame(frame);
    gl3d_raster(g_menu_buffer1, mode[0], mode[1], mode[0], s_cmds, n);
    return 0;
}
