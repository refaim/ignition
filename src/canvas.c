/*
 * canvas.c - the game's 8 bit screens as GPU render targets.
 *
 * A surface keeps the colour indices in an R8 texture at the internal
 * resolution, with a second texture to copy into before drawing that
 * reads the pixel under it, and a depth buffer for the 3D.
 *
 * A colour table T gives T[s << 8 | d] for source s over destination
 * d. Most rows are the identity (s leaves d alone) or one colour (s is
 * opaque); only the other rows need d. Each table is classed row by row
 * when first used, so drawing copies the surface only when a source
 * colour can blend. The GPU work goes through rnd.h.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "lisa/gfx.h"
#include "rnd.h"
#include "video.h"

enum {
    MAX_SURFACES = 8,
    MAX_TABLES = 32,
    MAX_SPRITES = 1024,
    ROW_IDENTITY = 0,
    ROW_CONSTANT = 1,
    ROW_BLEND = 2
};

struct surface {
    u8 *key;                /* registered; GPU objects only in hardware */
    int lw, lh;             /* logical, the original's pixels */
    float mx, my;           /* NATIVE's margins about them, logical */
    int fit;                /* NATIVE fills the height, not a whole scale */
    int margin;             /* the margins' colour, -1 the picture's ground */
    int pw, ph;             /* physical, margins included */
    unsigned target, tex, copy;
    /* What was drawn since `copy` was taken (logical rectangles); a
     * blended draw copies the surface again only when it covers them.
     * Each copy costs the Vita a scene of its own. */
    int copy_ok, dirty_n;
    float dirty[32][4];
};

struct table {
    const u8 *table;
    unsigned tex;           /* R8 256 x 256, row = source */
    unsigned rows;          /* RG8 256 x 1: class, colour */
    int blends;             /* some row needs the destination */
};

struct sprite_tex {
    const u8 *pixels;
    int w, h, stride;
    unsigned tex;
    u32 used;
};

static int s_ready;
static int s_mode = CANVAS_NATIVE;
static int s_forced = -1;
static int s_hardware = 1;      /* 0 draws the 2D on the CPU, as the original */
static struct surface s_surfaces[MAX_SURFACES];
static struct table s_tables[MAX_TABLES];
static int s_table_count;
static struct sprite_tex s_sprites[MAX_SPRITES];
static u32 s_clock;
static unsigned s_upload;

int canvas_init(void)
{
    s_ready = 1;
    return 1;
}

int canvas_ready(void)
{
    return s_ready;
}

/* The internal resolution's height, and the width of a 4:3 picture
 * of it: a logical 320 x 200 is shown 4:3. */
void canvas_internal_size(int *w43, int *h)
{
    static const int heights[3] = { 200, 480, 600 };
    int dw = 0, dh = 0;

    if (s_mode >= CANVAS_320X200 && s_mode <= CANVAS_800X600) {
        *h = heights[s_mode];
    } else {
        video_drawable_size(&dw, &dh);
        *h = dh > 0 ? dh : 600;
    }
    *w43 = s_mode == CANVAS_320X200 ? 320 : (*h * 4 + 1) / 3;
}

static void surface_free(struct surface *s)
{
    if (s->target != 0) {
        rnd_target_free(s->target);
        rnd_texture_free(s->tex);
        rnd_texture_free(s->copy);
    }
    s->target = s->tex = s->copy = 0;
}

/* In NATIVE a 320 x 200 screen fills the drawable: its pixels are
 * shown at a whole scale of 2 (4 from 1200 rows, as such a drawable is
 * twice the window's points), 1.2 times as tall as wide, and the rest
 * of the drawable is a margin about them. Returns 0 for no margins. */
static int native_margins(const struct surface *s, float *mx, float *my, int *pw, int *ph)
{
    int dw = 0, dh = 0;
    double k;

    if (s_mode != CANVAS_NATIVE || s->lw != 320 || s->lh != 200) {
        return 0;
    }
    video_drawable_size(&dw, &dh);
    if (dw < 320 || dh < 240) {
        return 0;
    }
    k = dh >= 1200 ? 4.0 : 2.0;
    if (s->fit) {
        k = fmin(dw / 320.0, dh / 240.0);
    } else if (320.0 * k > dw || 240.0 * k > dh) {
        k = floor(fmin(dw / 320.0, dh / 240.0));
    }
    *mx = (float)((dw / k - 320.0) / 2.0);
    *my = (float)((dh / (k * 1.2) - 200.0) / 2.0);
    *pw = dw;
    *ph = dh;
    return 1;
}

static const float s_zero[4] = { 0, 0, 0, 0 };

static void surface_make(struct surface *s)
{
    int w43, h;

    s->mx = s->my = 0;
    if (!native_margins(s, &s->mx, &s->my, &s->pw, &s->ph)) {
        canvas_internal_size(&w43, &h);
        s->pw = (int)((double)s->lw * w43 / 320.0 + 0.5);
        s->ph = (int)((double)s->lh * h / 200.0 + 0.5);
    }
    s->tex = rnd_texture(RND_R8, s->pw, s->ph, NULL, 0);
    s->copy = rnd_texture(RND_R8, s->pw, s->ph, NULL, 0);
    s->target = rnd_target(s->tex, 1);
    s->copy_ok = 0;
    s->dirty_n = 0;
    rnd_bind(s->target);
    rnd_clear(s_zero, 1);
}

void canvas_force_resolution(int mode)
{
    s_forced = mode;
}

void canvas_set_resolution(int mode)
{
    int i;

    s_mode = s_forced >= 0 ? s_forced : mode;
    if (!s_ready) {
        return;
    }
    for (i = 0; i < MAX_SURFACES; i++) {
        if (s_surfaces[i].key != NULL && s_hardware) {
            surface_free(&s_surfaces[i]);
            surface_make(&s_surfaces[i]);
        }
    }
}

/* NATIVE follows the drawable: when it changes size, the surfaces are
 * made again (empty; the menus draw theirs each frame). */
void canvas_sync_drawable(void)
{
    static int last_w, last_h;
    int w = 0, h = 0;

    if (!s_ready || s_mode != CANVAS_NATIVE || !s_hardware) {
        return;
    }
    video_drawable_size(&w, &h);
    if (w != last_w || h != last_h) {
        last_w = w;
        last_h = h;
        canvas_set_resolution(CANVAS_NATIVE);
    }
}

int canvas_resolution(void)
{
    return s_mode;
}

static struct surface *find(const u8 *key)
{
    int i;

    if (!s_ready || key == NULL) {
        return NULL;
    }
    for (i = 0; i < MAX_SURFACES; i++) {
        if (s_surfaces[i].key == key) {
            return s_hardware && s_surfaces[i].target != 0 ? &s_surfaces[i] : NULL;
        }
    }
    return NULL;
}

static struct surface *registered(const u8 *key)
{
    int i;

    for (i = 0; i < MAX_SURFACES; i++) {
        if (s_surfaces[i].key == key) {
            return &s_surfaces[i];
        }
    }
    return NULL;
}

void canvas_set_hardware(int on)
{
    int i;

    s_hardware = on != 0;
    if (!s_ready) {
        return;
    }
    for (i = 0; i < MAX_SURFACES; i++) {
        struct surface *s = &s_surfaces[i];

        if (s->key == NULL) {
            continue;
        }
        surface_free(s);
        if (s_hardware) {
            surface_make(s);
        }
    }
}

int canvas_hardware(void)
{
    return s_hardware;
}

void canvas_register(u8 *key, int lw, int lh)
{
    struct surface *s = registered(key);
    int i;

    if (!s_ready || key == NULL) {
        return;
    }
    if (s == NULL) {
        for (i = 0; i < MAX_SURFACES && s_surfaces[i].key != NULL; i++) {
        }
        if (i == MAX_SURFACES) {
            return;
        }
        s = &s_surfaces[i];
    } else if (s->lw == lw && s->lh == lh) {
        return;
    } else {
        surface_free(s);
    }
    s->key = key;
    s->margin = -1;
    s->lw = lw;
    s->lh = lh;
    if (s_hardware) {
        surface_make(s);
    }
}

void canvas_unregister(u8 *key)
{
    struct surface *s = registered(key);

    if (s != NULL) {
        surface_free(s);
        s->key = NULL;
    }
}

int canvas_is_gpu(const u8 *key)
{
    return find(key) != NULL;
}

/* Binds a surface, for 2D drawing. */
static void bind(struct surface *s)
{
    float lsize[2], off[2];

    lsize[0] = s->lw + 2 * s->mx;
    lsize[1] = s->lh + 2 * s->my;
    off[0] = s->mx;
    off[1] = s->my;
    rnd_bind(s->target);
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_cull(RND_CULL_OFF);
    rnd_use(RND_CANVAS);
    rnd_uniform(RND_U_LSIZE, lsize);
    rnd_uniform(RND_U_OFF, off);
}

static void set_mode(int mode)
{
    float m = (float)mode;

    rnd_uniform(RND_U_MODE, &m);
}

/* Clears the bound surface's colour to an index. */
static void clear_index(int index)
{
    float c[4] = { 0, 0, 0, 0 };

    c[0] = (float)index / 255.0f;
    rnd_clear(c, 0);
}

/* Logical to physical, margins included. */
static float phys_x(const struct surface *s, float x)
{
    return (x + s->mx) * s->pw / (s->lw + 2 * s->mx);
}

static float phys_y(const struct surface *s, float y)
{
    return (y + s->my) * s->ph / (s->lh + 2 * s->my);
}

void canvas_set_fit(const u8 *key, int on)
{
    struct surface *s = registered(key);

    on = on != 0;
    if (s == NULL || s->fit == on) {
        return;
    }
    s->fit = on;
    s->margin = -1;
    if (s_ready && s_hardware && s->target != 0) {
        surface_free(s);
        surface_make(s);
    }
}

void canvas_set_margin(const u8 *key, int index)
{
    struct surface *s = registered(key);

    if (s != NULL) {
        s->margin = index;
    }
}

void canvas_extent(const u8 *key, float *mx, float *my)
{
    struct surface *s = find(key);

    *mx = s != NULL ? s->mx : 0;
    *my = s != NULL ? s->my : 0;
}

float canvas_aspect(const u8 *key, float fallback)
{
    struct surface *s = find(key);

    if (s == NULL || (s->mx == 0 && s->my == 0)) {
        return fallback;
    }
    return (float)s->pw / (float)s->ph;
}

/* The whole surface changed (cleared, put, copied over, or drawn by
 * gl3d): the copy no longer matches it anywhere. */
static void dirty_all(struct surface *s)
{
    s->copy_ok = 0;
}

static void dirty_rect(struct surface *s, float x0, float y0, float x1, float y1)
{
    if (s->dirty_n == 32) {
        s->copy_ok = 0;
        return;
    }
    s->dirty[s->dirty_n][0] = x0;
    s->dirty[s->dirty_n][1] = y0;
    s->dirty[s->dirty_n][2] = x1;
    s->dirty[s->dirty_n][3] = y1;
    s->dirty_n++;
}

/* Whether the copy is out of date under the rectangle. */
static int copy_stale(const struct surface *s, float x0, float y0, float x1, float y1)
{
    int i;

    if (!s->copy_ok) {
        return 1;
    }
    for (i = 0; i < s->dirty_n; i++) {
        const float *d = s->dirty[i];

        if (x0 < d[2] + 1 && d[0] - 1 < x1 && y0 < d[3] + 1 && d[1] - 1 < y1) {
            return 1;
        }
    }
    return 0;
}

void canvas_clear(u8 *key, u8 index, size_t bytes)
{
    struct surface *s = find(key);

    if (s == NULL) {
        memset(key, index, bytes);
        return;
    }
    rnd_bind(s->target);
    clear_index(index);
    dirty_all(s);
}

/* Draws the quad (x0, y0)..(x1, y1) in logical pixels with texture
 * coordinates (u0, v0)..(u1, v1) in texels. */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    float v[16] = {
        x0, y0, u0, v0,  x1, y0, u1, v0,  x0, y1, u0, v1,  x1, y1, u1, v1
    };

    rnd_stream(v, 4);
    rnd_draw(RND_STRIP, 0, 4);
}

/* A CPU image as a texture, for the next draw to read. */
static unsigned upload_src(const u8 *pixels, int w, int h, int pitch)
{
    if (s_upload == 0) {
        s_upload = rnd_texture(RND_R8, w, h, pixels, pitch);
    } else {
        rnd_texture_set(s_upload, w, h, pixels, pitch);
    }
    return s_upload;
}

void canvas_put(u8 *dst, const u8 *pixels, int pitch)
{
    struct surface *s = find(dst);
    unsigned tex;

    if (s == NULL) {
        int y;

        if (dst != pixels) {
            for (y = 0; y < 200; y++) {
                memmove(dst + y * pitch, pixels + y * pitch, (size_t)pitch);
            }
        }
        return;
    }
    tex = upload_src(pixels, s->lw, s->lh, pitch);
    bind(s);
    if (s->mx != 0 || s->my != 0) {
        /* A whole screen put: the margins take its first pixel's
         * colour, the picture's ground. */
        clear_index(s->margin >= 0 ? s->margin : pixels[0]);
    }
    set_mode(0);
    rnd_texture_unit(0, tex);
    quad(0, 0, (float)s->lw, (float)s->lh, 0, 0, (float)s->lw, (float)s->lh);
    dirty_all(s);
}

void canvas_copy(u8 *dst, const u8 *src, size_t bytes)
{
    struct surface *d = find(dst), *s = find(src);

    if (d == NULL && s == NULL) {
        memcpy(dst, src, bytes);
        return;
    }
    if (d != NULL && s != NULL) {
        rnd_blit(s->target, d->target, s->pw, s->ph, d->pw, d->ph);
        dirty_all(d);
        return;
    }
    if (d != NULL) {
        canvas_put(dst, src, d->lw);
        return;
    }
    fprintf(stderr, "canvas: copy from a surface to memory\n");
}

static struct table *table_get(const u8 *table)
{
    u8 rows[512];
    struct table *t;
    int i, s, d;

    for (i = 0; i < s_table_count; i++) {
        if (s_tables[i].table == table) {
            return &s_tables[i];
        }
    }
    if (s_table_count == MAX_TABLES) {
        rnd_texture_free(s_tables[0].tex);
        rnd_texture_free(s_tables[0].rows);
        memmove(s_tables, s_tables + 1, (MAX_TABLES - 1) * sizeof(s_tables[0]));
        s_table_count--;
    }
    t = &s_tables[s_table_count++];
    t->table = table;
    t->blends = 0;
    for (s = 0; s < 256; s++) {
        const u8 *row = table + s * 256;
        int identity = 1, constant = 1;

        for (d = 0; d < 256; d++) {
            identity &= row[d] == d;
            constant &= row[d] == row[0];
        }
        rows[2 * s] = identity ? ROW_IDENTITY : constant ? ROW_CONSTANT : ROW_BLEND;
        rows[2 * s + 1] = row[0];
        t->blends |= rows[2 * s] == ROW_BLEND;
    }
    t->tex = rnd_texture(RND_R8, 256, 256, table, 256);
    t->rows = rnd_texture(RND_RG8, 256, 1, rows, 256);
    return t;
}

static unsigned sprite_tex(const u8 *pixels, int w, int h, int stride)
{
    struct sprite_tex *e, *old = &s_sprites[0];
    int i;

    s_clock++;
    for (i = 0; i < MAX_SPRITES; i++) {
        e = &s_sprites[i];
        if (e->pixels == pixels && e->w == w && e->h == h && e->stride == stride) {
            e->used = s_clock;
            return e->tex;
        }
        if (e->used < old->used) {
            old = e;
        }
    }
    if (old->tex != 0) {
        rnd_texture_free(old->tex);
    }
    old->tex = rnd_texture(RND_R8, w, h, pixels, stride);
    old->pixels = pixels;
    old->w = w;
    old->h = h;
    old->stride = stride;
    old->used = s_clock;
    return old->tex;
}

void canvas_forget(const u8 *pixels)
{
    int i;

    if (!s_ready) {
        return;
    }
    for (i = 0; i < MAX_SPRITES; i++) {
        if (s_sprites[i].pixels == pixels && s_sprites[i].tex != 0) {
            rnd_texture_free(s_sprites[i].tex);
            memset(&s_sprites[i], 0, sizeof(s_sprites[i]));
        }
    }
}

/* Copies the part of the surface a draw covers (logical rectangle) into
 * its copy texture, for the shader to read the pixels under it. The
 * surface is bound. */
static void snapshot_rect(struct surface *s, float x0, float y0, float x1, float y1)
{
    int px0 = (int)floorf(phys_x(s, x0)) - 1, py0 = (int)floorf(phys_y(s, y0)) - 1;
    int px1 = (int)ceilf(phys_x(s, x1)) + 1, py1 = (int)ceilf(phys_y(s, y1)) + 1;

    if (px0 < 0) {
        px0 = 0;
    }
    if (py0 < 0) {
        py0 = 0;
    }
    if (px1 > s->pw) {
        px1 = s->pw;
    }
    if (py1 > s->ph) {
        py1 = s->ph;
    }
    if (px1 <= px0 || py1 <= py0) {
        return;
    }
    rnd_copy(s->copy, px0, py0, px1 - px0, py1 - py0);
}

/* Binds the table and, if it can blend, the pixels under the
 * rectangle: a copy of the whole surface, taken again only where it is
 * stale, so draws that do not overlap share one. */
static void use_table(struct surface *s, const u8 *table, float x0, float y0, float x1, float y1)
{
    struct table *t = table_get(table);

    if (t->blends && copy_stale(s, x0, y0, x1, y1)) {
        rnd_copy(s->copy, 0, 0, s->pw, s->ph);
        s->copy_ok = 1;
        s->dirty_n = 0;
    }
    dirty_rect(s, x0, y0, x1, y1);
    rnd_texture_unit(1, t->tex);
    rnd_texture_unit(2, t->rows);
    rnd_texture_unit(3, s->copy);
    set_mode(1);
}

static void scissor(struct surface *s)
{
    const struct gfx_target *g = &g_gfx_target;
    int x0 = (int)phys_x(s, (float)g->clip_x0), y0 = (int)phys_y(s, (float)g->clip_y0);
    int x1 = (int)phys_x(s, (float)g->clip_x1), y1 = (int)phys_y(s, (float)g->clip_y1);

    /* A clip of the whole screen takes in the margins. */
    if (g->clip_x0 <= 0 && g->clip_y0 <= 0 && g->clip_x1 >= s->lw && g->clip_y1 >= s->lh) {
        x0 = y0 = 0;
        x1 = s->pw;
        y1 = s->ph;
    }

    rnd_scissor(1, x0, y0, x1 - x0, y1 - y0);
}

int canvas_sprite(const u8 *pixels, int w, int h, int stride, s32 x, s32 y, s32 hx, s32 hy,
                  const s32 *m)
{
    struct surface *s = find(g_gfx_target.base);
    unsigned tex;
    float fx = x / 256.0f, fy = y / 256.0f;

    if (s == NULL || pixels == NULL) {
        return s != NULL;
    }
    tex = sprite_tex(pixels, w, h, stride);
    bind(s);
    scissor(s);
    if (m == NULL) {
        use_table(s, g_gfx_color_table, fx, fy, fx + w, fy + h);
        rnd_texture_unit(0, tex);
        quad(fx, fy, fx + w, fy + h, 0, 0, (float)w, (float)h);
    } else {
        /* Corners of the texture about its hot spot, through m. */
        float a = m[0] / 65536.0f, b = m[1] / 65536.0f, c = m[2] / 65536.0f,
              d = m[3] / 65536.0f;
        float cx = (x + hx) / 256.0f, cy = (y + hy) / 256.0f;
        float hxf = hx / 256.0f, hyf = hy / 256.0f;
        float px[4], py[4], v[16], lo_x, lo_y, hi_x, hi_y;
        static const float us[4] = { 0, 1, 0, 1 }, vs[4] = { 0, 0, 1, 1 };
        int k;

        for (k = 0; k < 4; k++) {
            float tx = us[k] * w - hxf, ty = vs[k] * h - hyf;

            px[k] = cx + a * tx - b * ty;
            py[k] = cy + c * tx + d * ty;
            v[4 * k] = px[k];
            v[4 * k + 1] = py[k];
            v[4 * k + 2] = us[k] * w;
            v[4 * k + 3] = vs[k] * h;
        }
        lo_x = hi_x = px[0];
        lo_y = hi_y = py[0];
        for (k = 1; k < 4; k++) {
            lo_x = px[k] < lo_x ? px[k] : lo_x;
            hi_x = px[k] > hi_x ? px[k] : hi_x;
            lo_y = py[k] < lo_y ? py[k] : lo_y;
            hi_y = py[k] > hi_y ? py[k] : hi_y;
        }
        use_table(s, g_gfx_color_table, lo_x, lo_y, hi_x, hi_y);
        rnd_texture_unit(0, tex);
        rnd_stream(v, 4);
        rnd_draw(RND_STRIP, 0, 4);
    }
    rnd_scissor(0, 0, 0, 0, 0);
    return 1;
}

int canvas_blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                      const u8 *table, int src_pitch)
{
    struct surface *d = find(dst), *s = find(src);
    int w = x1 - x0, h = y1 - y0;
    unsigned tex;

    if (d == NULL) {
        return 0;
    }
    if (w <= 0 || h <= 0) {
        return 1;
    }
    bind(d);
    if (s != NULL) {
        /* A surface's texture is physical; draw it at its own scale. */
        fprintf(stderr, "canvas: blend from a surface not supported\n");
        return 1;
    }
    tex = upload_src(src + y0 * src_pitch + x0, w, h, src_pitch);
    use_table(d, table, (float)dx, (float)dy, (float)(dx + w), (float)(dy + h));
    rnd_texture_unit(0, tex);
    quad((float)dx, (float)dy, (float)(dx + w), (float)(dy + h), 0, 0, (float)w, (float)h);
    return 1;
}

void canvas_remap(u8 *dst, const u8 *levels, const u8 *table)
{
    struct surface *d = find(dst);
    unsigned tex;

    if (d == NULL) {
        return;
    }
    tex = upload_src(levels, d->lw, d->lh, d->lw);
    bind(d);
    use_table(d, table, -d->mx, -d->my, d->lw + d->mx, d->lh + d->my);
    rnd_texture_unit(0, tex);
    /* The levels' edges carry on over the margins. */
    quad(-d->mx, -d->my, d->lw + d->mx, d->lh + d->my, -d->mx, -d->my, d->lw + d->mx,
         d->lh + d->my);
}

unsigned canvas_bind(u8 *key, int *pw, int *ph, int clear_depth)
{
    struct surface *s = find(key);

    if (s == NULL) {
        return 0;
    }
    rnd_bind(s->target);
    if (clear_depth) {
        rnd_clear(NULL, 1);
    }
    /* gl3d draws into it. */
    dirty_all(s);
    *pw = s->pw;
    *ph = s->ph;
    return s->target;
}

unsigned canvas_texture(const u8 *key)
{
    struct surface *s = find(key);

    return s != NULL ? s->tex : 0;
}

unsigned canvas_snapshot(const u8 *key)
{
    struct surface *s = find(key);

    if (s == NULL) {
        return 0;
    }
    rnd_bind(s->target);
    rnd_copy(s->copy, 0, 0, s->pw, s->ph);
    s->copy_ok = 1;
    s->dirty_n = 0;
    return s->copy;
}

unsigned canvas_snapshot_rect(const u8 *key, float x0, float y0, float x1, float y1)
{
    struct surface *s = find(key);

    if (s == NULL) {
        return 0;
    }
    snapshot_rect(s, x0, y0, x1, y1);
    /* Only part of the copy is fresh. */
    dirty_all(s);
    return s->copy;
}
