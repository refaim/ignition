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
 * colour can blend.
 */

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "lisa/gfx.h"
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
    int pw, ph;             /* physical */
    GLuint fbo, tex, copy, depth;
};

struct table {
    const u8 *table;
    GLuint tex;             /* R8 256 x 256, row = source */
    GLuint rows;            /* RG8 256 x 1: class, colour */
    int blends;             /* some row needs the destination */
};

struct sprite_tex {
    const u8 *pixels;
    int w, h, stride;
    GLuint tex;
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
static GLuint s_program, s_vao, s_vbo, s_upload;
static int s_upload_w, s_upload_h;
static GLint s_u_lsize, s_u_mode;

static const char *s_vs =
    "#version 150\n"
    "uniform vec2 u_lsize;\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = vec4(a_pos / u_lsize * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Mode 0 copies the source index, 1 puts it through the table. */
static const char *s_fs =
    "#version 150\n"
    "uniform sampler2D u_src;\n"
    "uniform sampler2D u_table;\n"
    "uniform sampler2D u_rows;\n"
    "uniform sampler2D u_dst;\n"
    "uniform int u_mode;\n"
    "in vec2 v_uv;\n"
    "out float o_index;\n"
    "void main() {\n"
    "    ivec2 size = textureSize(u_src, 0);\n"
    "    ivec2 t = clamp(ivec2(floor(v_uv)), ivec2(0), size - 1);\n"
    "    float s = texelFetch(u_src, t, 0).r;\n"
    "    if (u_mode == 1) {\n"
    "        int si = int(s * 255.0 + 0.5);\n"
    "        vec2 row = texelFetch(u_rows, ivec2(si, 0), 0).rg;\n"
    "        int cls = int(row.r * 255.0 + 0.5);\n"
    "        if (cls == 0) discard;\n"
    "        if (cls == 1) {\n"
    "            s = row.g;\n"
    "        } else {\n"
    "            float d = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0).r;\n"
    "            s = texelFetch(u_table, ivec2(int(d * 255.0 + 0.5), si), 0).r;\n"
    "        }\n"
    "    }\n"
    "    o_index = s;\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "canvas: shader: %s\n", log);
    }
    return s;
}

static GLuint r_texture(GLenum format, int w, int h, const void *pixels)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format == GL_RG8 ? GL_RG : GL_RED,
                 GL_UNSIGNED_BYTE, pixels);
    return t;
}

int canvas_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, s_vs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fs);

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glBindAttribLocation(s_program, 0, "a_pos");
    glBindAttribLocation(s_program, 1, "a_uv");
    glBindFragDataLocation(s_program, 0, "o_index");
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(s_program);
    glUniform1i(glGetUniformLocation(s_program, "u_src"), 0);
    glUniform1i(glGetUniformLocation(s_program, "u_table"), 1);
    glUniform1i(glGetUniformLocation(s_program, "u_rows"), 2);
    glUniform1i(glGetUniformLocation(s_program, "u_dst"), 3);
    s_u_lsize = glGetUniformLocation(s_program, "u_lsize");
    s_u_mode = glGetUniformLocation(s_program, "u_mode");
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
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
    if (s->fbo != 0) {
        glDeleteFramebuffers(1, &s->fbo);
        glDeleteTextures(1, &s->tex);
        glDeleteTextures(1, &s->copy);
        glDeleteRenderbuffers(1, &s->depth);
    }
    s->fbo = s->tex = s->copy = s->depth = 0;
}

static void surface_make(struct surface *s)
{
    int w43, h;

    canvas_internal_size(&w43, &h);
    s->pw = (int)((double)s->lw * w43 / 320.0 + 0.5);
    s->ph = (int)((double)s->lh * h / 200.0 + 0.5);
    s->tex = r_texture(GL_R8, s->pw, s->ph, NULL);
    s->copy = r_texture(GL_R8, s->pw, s->ph, NULL);
    glGenRenderbuffers(1, &s->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, s->pw, s->ph);
    glGenFramebuffers(1, &s->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->depth);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
            return s_hardware && s_surfaces[i].fbo != 0 ? &s_surfaces[i] : NULL;
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

/* Binds a surface's framebuffer and viewport, for 2D drawing. */
static void bind(struct surface *s)
{
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glViewport(0, 0, s->pw, s->ph);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(s_program);
    glBindVertexArray(s_vao);
    glUniform2f(s_u_lsize, (float)s->lw, (float)s->lh);
}

void canvas_clear(u8 *key, u8 index, size_t bytes)
{
    struct surface *s = find(key);

    if (s == NULL) {
        memset(key, index, bytes);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glClearColor(index / 255.0f, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Draws the quad (x0, y0)..(x1, y1) in logical pixels with texture
 * coordinates (u0, v0)..(u1, v1) in texels. */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    float v[16] = {
        x0, y0, u0, v0,  x1, y0, u1, v0,  x0, y1, u0, v1,  x1, y1, u1, v1
    };

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void upload_src(const u8 *pixels, int w, int h, int pitch)
{
    glActiveTexture(GL_TEXTURE0);
    if (s_upload == 0 || s_upload_w != w || s_upload_h != h) {
        if (s_upload != 0) {
            glDeleteTextures(1, &s_upload);
        }
        s_upload = r_texture(GL_R8, w, h, NULL);
        s_upload_w = w;
        s_upload_h = h;
    }
    glBindTexture(GL_TEXTURE_2D, s_upload);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void canvas_put(u8 *dst, const u8 *pixels, int pitch)
{
    struct surface *s = find(dst);

    if (s == NULL) {
        int y;

        if (dst != pixels) {
            for (y = 0; y < 200; y++) {
                memmove(dst + y * pitch, pixels + y * pitch, (size_t)pitch);
            }
        }
        return;
    }
    upload_src(pixels, s->lw, s->lh, pitch);
    bind(s);
    glUniform1i(s_u_mode, 0);
    quad(0, 0, (float)s->lw, (float)s->lh, 0, 0, (float)s->lw, (float)s->lh);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void canvas_copy(u8 *dst, const u8 *src, size_t bytes)
{
    struct surface *d = find(dst), *s = find(src);

    if (d == NULL && s == NULL) {
        memcpy(dst, src, bytes);
        return;
    }
    if (d != NULL && s != NULL) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->fbo);
        glBlitFramebuffer(0, 0, s->pw, s->ph, 0, 0, d->pw, d->ph, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
        glDeleteTextures(1, &s_tables[0].tex);
        glDeleteTextures(1, &s_tables[0].rows);
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
    t->tex = r_texture(GL_R8, 256, 256, table);
    t->rows = r_texture(GL_RG8, 256, 1, rows);
    return t;
}

static GLuint sprite_tex(const u8 *pixels, int w, int h, int stride)
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
        glDeleteTextures(1, &old->tex);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    old->tex = r_texture(GL_R8, w, h, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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
            glDeleteTextures(1, &s_sprites[i].tex);
            memset(&s_sprites[i], 0, sizeof(s_sprites[i]));
        }
    }
}

/* Copies the part of the surface a draw covers (logical rectangle) into
 * its copy texture, for the shader to read the pixels under it. */
static void snapshot_rect(struct surface *s, float x0, float y0, float x1, float y1)
{
    int px0 = (int)floorf(x0 * s->pw / s->lw) - 1, py0 = (int)floorf(y0 * s->ph / s->lh) - 1;
    int px1 = (int)ceilf(x1 * s->pw / s->lw) + 1, py1 = (int)ceilf(y1 * s->ph / s->lh) + 1;

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
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s->copy);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, px0, py0, px0, py0, px1 - px0, py1 - py0);
}

/* Binds the table and, if it can blend, the pixels under the
 * rectangle. */
static void use_table(struct surface *s, const u8 *table, float x0, float y0, float x1, float y1)
{
    struct table *t = table_get(table);

    if (t->blends) {
        snapshot_rect(s, x0, y0, x1, y1);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, t->rows);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(s_u_mode, 1);
}

static void scissor(struct surface *s)
{
    const struct gfx_target *g = &g_gfx_target;
    int x0 = g->clip_x0 * s->pw / s->lw, y0 = g->clip_y0 * s->ph / s->lh;
    int x1 = g->clip_x1 * s->pw / s->lw, y1 = g->clip_y1 * s->ph / s->lh;

    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, y0, x1 - x0, y1 - y0);
}

int canvas_sprite(const u8 *pixels, int w, int h, int stride, s32 x, s32 y, s32 hx, s32 hy,
                  const s32 *m)
{
    struct surface *s = find(g_gfx_target.base);
    GLuint tex;
    float fx = x / 256.0f, fy = y / 256.0f;

    if (s == NULL || pixels == NULL) {
        return s != NULL;
    }
    tex = sprite_tex(pixels, w, h, stride);
    bind(s);
    scissor(s);
    if (m == NULL) {
        use_table(s, g_gfx_color_table, fx, fy, fx + w, fy + h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
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
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

int canvas_blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                      const u8 *table, int src_pitch)
{
    struct surface *d = find(dst), *s = find(src);
    int w = x1 - x0, h = y1 - y0;

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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 1;
    }
    upload_src(src + y0 * src_pitch + x0, w, h, src_pitch);
    use_table(d, table, (float)dx, (float)dy, (float)(dx + w), (float)(dy + h));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_upload);
    quad((float)dx, (float)dy, (float)(dx + w), (float)(dy + h), 0, 0, (float)w, (float)h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

void canvas_remap(u8 *dst, const u8 *levels, const u8 *table)
{
    struct surface *d = find(dst);

    if (d == NULL) {
        return;
    }
    upload_src(levels, d->lw, d->lh, d->lw);
    bind(d);
    use_table(d, table, 0, 0, (float)d->lw, (float)d->lh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_upload);
    quad(0, 0, (float)d->lw, (float)d->lh, 0, 0, (float)d->lw, (float)d->lh);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned canvas_bind(u8 *key, int *pw, int *ph, int clear_depth)
{
    struct surface *s = find(key);

    if (s == NULL) {
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glViewport(0, 0, s->pw, s->ph);
    if (clear_depth) {
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    *pw = s->pw;
    *ph = s->ph;
    return s->fbo;
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
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s->copy);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, s->pw, s->ph);
    glActiveTexture(GL_TEXTURE0);
    return s->copy;
}

unsigned canvas_snapshot_rect(const u8 *key, float x0, float y0, float x1, float y1)
{
    struct surface *s = find(key);

    if (s == NULL) {
        return 0;
    }
    snapshot_rect(s, x0, y0, x1, y1);
    glActiveTexture(GL_TEXTURE0);
    return s->copy;
}
