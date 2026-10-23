/*
 * gl3d.c - the scene's objects through OpenGL.
 *
 * The original projects each vertex as
 *     sx = cx - focal_x * zoom * Xc / (Zc + near)
 *     sy = cy - focal_y * Yc / (Zc + near)
 * with Xc, Yc, Zc the view coordinates (0x449e70), and draws the faces
 * back to front. Here the same projection is one matrix per object,
 * with w = Zc + near, and a depth buffer orders the faces. Faces are
 * clipped at the near plane, which the original does not do.
 */

#include "gl_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "gl3d.h"

enum {
    MAX_MESHES = 1024,
    MAX_TEXTURES = 64,
    FLOATS = 8          /* x, y, z, u, v, page, colour, mode */
};

/* How a face's pixels come out. */
enum {
    MODE_TEXTURE = 0,   /* the texel */
    MODE_KEYED = 1,     /* the texel, colour 0 left out */
    MODE_FLAT = 2,      /* the face's colour */
    MODE_GRADIENT = 3   /* a colour from u, across a line */
};

struct mesh_entry {
    const s32 *mesh;
    GLint first;
    GLsizei count;
    int deferred;               /* has faces through a table */
};

struct texture_entry {
    const u8 *pixels;
    int pages;
    GLuint tex;
    int cols, rows;             /* Vita: the tiles' layout (pages_texture) */
};

static int s_ready;
static GLuint s_program, s_vao, s_vbo;
static GLint s_u_matrix, s_u_pages;
static GLuint s_fbo;            /* the framebuffer drawn into */
/* Render targets: 0 is read back over an 8 bit screen, 1 is the view
 * kept on the GPU. */
struct target {
    GLuint fbo, color, depth;
    int w, h;
};

static struct target s_targets[2];
static struct target *s_target;
static int s_w, s_h;            /* the target's size */
static double s_vw, s_vh;       /* the original's pixels it spans */
static double s_ox, s_oy;       /* where the original's (0, 0) is in them */
static struct mesh_entry s_meshes[MAX_MESHES];
static int s_mesh_count;
static GLint s_vbo_used, s_vbo_size;
static struct texture_entry s_textures[MAX_TEXTURES];
static int s_texture_count;
static u8 *s_readback;
static size_t s_readback_size;

/* Faces drawn after the rest, each frame anew: points and sprites
 * (0x44b5e0, 0x44d0f0, 0x44d230), whose meshes change as the effects
 * move, and faces through a table (19, 23: the .SHD; 0x44cb20), which
 * read the pixels under them. Kept as clip space vertices. */
enum { DYN_FLOATS = 9, MAX_DYN = 4096 };    /* x, y, z, w, u, v, page, colour, mode */

struct dyn {
    float v[6][DYN_FLOATS];
    int count;                  /* 3 or 6 vertices */
    float depth;                /* w of the middle, for the order */
    GLuint tex;
    const u8 *table;            /* NULL: drawn as it is */
};

static struct dyn s_dyns[MAX_DYN];
static int s_dyn_count;
static const u8 *s_tables_shd, *s_tables_fixed;     /* 0x63b5f0, 0x63c5f4 */
static struct { const s32 *begin, *end; } s_dyn_ranges[8];    /* meshes always drawn anew */
static int s_dyn_range_count;
static GLuint s_dyn_program, s_dyn_vao, s_dyn_vbo, s_dyn_dst;
static GLint s_du_pages, s_du_table, s_du_dst, s_du_use_table;
static int s_dyn_dst_w, s_dyn_dst_h;

static GLuint table_get(const u8 *table);
static GLuint texture_get(int slot);

/* The raster path. */
enum { MAX_TABLES = 16, RASTER_FLOATS = 8 };     /* x, y, u, v, page, colour, mode, - */

struct table_entry {
    const u8 *table;
    GLuint tex;
};

static int s_raster_ready;
static GLuint s_raster_program, s_raster_vao, s_raster_vbo;
static GLint s_ru_size, s_ru_off, s_ru_pages, s_ru_table, s_ru_dst;
static GLuint s_raster_fbo, s_raster_color, s_raster_dst;
static int s_raster_w, s_raster_h;
static GLuint s_raster_pages;
static struct table_entry s_tables[MAX_TABLES];
static int s_table_count;
static float *s_raster_verts;
static int s_raster_cap;

#ifdef IGN_GL_VITA
/* Cg for vitaGL. A slot's pages are tiles of one 2D texture, u_grid.x a
 * row, u_grid.yz one over its size (pages_texel below); the varyings
 * are not flat, but a face's three vertices carry the same info. */
#define CG_PAGES \
    "float4 pages_texel(sampler2D pages, float3 grid, float page, float2 t) {\n" \
    "    float row = floor((page + 0.5) / grid.x);\n" \
    "    float col = page - row * grid.x;\n" \
    "    return tex2D(pages, (float2(col, row) * 256.0 + t + 0.5) * grid.yz);\n" \
    "}\n"

static GLint s_u_grid, s_du_grid, s_ru_grid, s_du_dst_size, s_ru_dst_size;

static const char *s_vs =
    "void main(float3 a_pos, float2 a_uv, float3 a_info, uniform float4x4 u_matrix,\n"
    "          float2 out v_uv : TEXCOORD0, float3 out v_info : TEXCOORD1,\n"
    "          float4 out gl_Position : POSITION) {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = mul(float4(a_pos, 1.0), u_matrix);\n"
    "}\n";

static const char *s_fs =
    CG_PAGES
    "float4 main(float2 v_uv : TEXCOORD0, float3 v_info : TEXCOORD1,\n"
    "            uniform sampler2D u_pages, uniform float3 u_grid) : COLOR {\n"
    "    float c = floor(v_info.y + 0.5);\n"
    "    if (v_info.z < 1.5) {\n"
    "        float2 t = min(floor(frac(v_uv) * 256.0), 255.0);\n"
    "        c = floor(pages_texel(u_pages, u_grid, floor(v_info.x + 0.5), t).r * 255.0 + 0.5);\n"
    "        if (v_info.z > 0.5 && c < 0.5) {\n"
    "            discard;\n"
    "        }\n"
    "    }\n"
    "    return float4((c + 0.25) / 255.0, 1.0, 0.0, 1.0);\n"
    "}\n";
#else
static const char *s_vs =
    "#version 150\n"
    "uniform mat4 u_matrix;\n"
    "in vec3 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec3 a_info;\n"
    "out vec2 v_uv;\n"
    "flat out vec3 v_info;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = u_matrix * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *s_fs =
    "#version 150\n"
    "uniform sampler2DArray u_pages;\n"
    "in vec2 v_uv;\n"
    "flat in vec3 v_info;\n"
    "out vec2 o_index;\n"
    "void main() {\n"
    "    float c = v_info.y;\n"
    "    if (v_info.z < 1.5) {\n"
    "        c = texture(u_pages, vec3(v_uv, v_info.x)).r * 255.0;\n"
    "        if (v_info.z > 0.5 && c < 0.5) discard;\n"
    "    }\n"
    "    o_index = vec2(c / 255.0, 1.0);\n"
    "}\n";
#endif

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type == GL_VERTEX_SHADER ? IGN_VERTEX_SHADER : IGN_FRAGMENT_SHADER);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "gl3d: shader: %s\n", log);
    }
    return s;
}

static void init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, s_vs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fs);

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glBindAttribLocation(s_program, 0, "a_pos");
    glBindAttribLocation(s_program, 1, "a_uv");
    glBindAttribLocation(s_program, 2, "a_info");
    glBindFragDataLocation(s_program, 0, "o_index");
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    s_u_matrix = glGetUniformLocation(s_program, "u_matrix");
    s_u_pages = glGetUniformLocation(s_program, "u_pages");
#ifdef IGN_GL_VITA
    s_u_grid = glGetUniformLocation(s_program, "u_grid");
#endif

    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    s_vbo_size = 1 << 16;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s_vbo_size * FLOATS * sizeof(float), NULL,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, FLOATS * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, FLOATS * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, FLOATS * sizeof(float),
                          (void *)(5 * sizeof(float)));
    s_ready = 1;
}

/* The size of a face of each type in bytes, 0 for the types that end a
 * mesh (the dispatch table at 0x49c8e0). */
static int face_size(int type)
{
    switch (type) {
    case 1: case 2: return 12;
    case 7: case 8: return 36;
    case 11: case 12: return 16;
    case 13: return 24;
    case 15: case 16: case 20: return 20;
    case 17: case 18: case 19: case 21: case 22: case 23: return 44;
    default: return 0;
    }
}

static void put(float *out, const s32 *mesh, s32 vi, float u, float v, float page,
                float colour, float mode)
{
    const s32 *p = mesh + 2 + 3 * vi;

    out[0] = (float)p[0];
    out[1] = (float)p[1];
    out[2] = (float)p[2];
    out[3] = u;
    out[4] = v;
    out[5] = page;
    out[6] = colour;
    out[7] = mode;
}

static void warn_once(int type)
{
    static u8 warned[256];

    if (!warned[type & 0xff]) {
        warned[type & 0xff] = 1;
        fprintf(stderr, "gl3d: face type %d not drawn yet\n", type);
    }
}

/* A mesh's faces as triangles into `verts`; returns their vertices. */
static int mesh_build(const s32 *mesh, float *verts, int *deferred_out)
{
    const u8 *f;
    int i, n = 0, deferred = 0;

    f = (const u8 *)(mesh + 2 + 3 * mesh[0]);
    for (i = 0; i < mesh[1]; i++) {
        const s32 *w = (const s32 *)f;
        int type = (s8)f[0];
        int size = face_size(type);
        float *o = verts + n * FLOATS;

        if (size == 0) {
            break;
        }
        if (type == 19 || type == 23 || type == 1 || type == 2 || type == 7 || type == 8
            || type == 13 || type == 20) {
            deferred = 1;
        } else if (size == 44 && type != 19 && type != 23) {
            float page = (float)((u32)w[10] >> 16);
            float mode = type == 18 || type == 22 ? MODE_KEYED : MODE_TEXTURE;
            int k;

            for (k = 0; k < 3; k++) {
                put(o + k * FLOATS, mesh, w[1 + k], w[4 + 2 * k] / 65536.0f,
                    w[5 + 2 * k] / 65536.0f, page, 0, mode);
            }
            n += 3;
        } else if (type == 15 || type == 16) {
            int k;

            for (k = 0; k < 3; k++) {
                put(o + k * FLOATS, mesh, w[1 + k], 0, 0, 0, (float)(w[4] & 0xff), MODE_FLAT);
            }
            n += 3;
        } else {
            warn_once(type);
        }
        f += size;
    }
    *deferred_out = deferred;
    return n;
}

/* Turns a mesh's faces into triangles, cached by the mesh's address. */
static struct mesh_entry *mesh_get(const s32 *mesh)
{
    struct mesh_entry *e;
    float *verts;
    int i, n, deferred;

    for (i = 0; i < s_mesh_count; i++) {
        if (s_meshes[i].mesh == mesh) {
            return &s_meshes[i];
        }
    }
    if (s_mesh_count == MAX_MESHES) {
        return NULL;
    }
    verts = malloc((size_t)mesh[1] * 3 * FLOATS * sizeof(float));
    n = mesh_build(mesh, verts, &deferred);
    if (s_vbo_used + n > s_vbo_size) {
        free(verts);
        return NULL;
    }
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)s_vbo_used * FLOATS * sizeof(float),
                    (GLsizeiptr)n * FLOATS * sizeof(float), verts);
    free(verts);
    e = &s_meshes[s_mesh_count++];
    e->mesh = mesh;
    e->first = s_vbo_used;
    e->count = n;
    e->deferred = deferred;
    s_vbo_used += n;
    return e;
}

/* A cached mesh's faces changed (the animated textures move their
 * coordinates): its triangles are built again in place. */
void gl3d_mesh_changed(const s32 *mesh)
{
    float *verts;
    int i, n, deferred;

    for (i = 0; i < s_mesh_count; i++) {
        if (s_meshes[i].mesh == mesh) {
            break;
        }
    }
    if (i == s_mesh_count) {
        return;
    }
    verts = malloc((size_t)mesh[1] * 3 * FLOATS * sizeof(float));
    n = mesh_build(mesh, verts, &deferred);
    if (n == s_meshes[i].count) {
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)s_meshes[i].first * FLOATS * sizeof(float),
                        (GLsizeiptr)n * FLOATS * sizeof(float), verts);
    }
    free(verts);
}

#ifdef IGN_GL_VITA
enum { PAGES_COLS = 16 };      /* 4096 texels, GXM's widest */

static int s_raster_cols, s_raster_rows;

/* `pages` pages of 256 x 256 as tiles of one texture, `*cols` to a row
 * and `*rows_out` rows. */
static GLuint pages_texture(const u8 *pixels, int pages, int *cols, int *rows_out)
{
    int c = pages < PAGES_COLS ? pages : PAGES_COLS, rows = (pages + c - 1) / c, i;
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, c * 256, rows * 256, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    for (i = 0; i < pages; i++) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, i % c * 256, i / c * 256, 256, 256, GL_RED,
                        GL_UNSIGNED_BYTE, pixels + (size_t)i * 256 * 256);
    }
    *cols = c;
    *rows_out = rows;
    return t;
}

/* Binds a pages texture and gives the program's u_grid its layout. */
static void bind_pages(GLint u_grid, GLuint tex)
{
    int cols = 1, rows = 1, i;

    if (tex != 0 && tex == s_raster_pages) {
        cols = s_raster_cols;
        rows = s_raster_rows;
    } else {
        for (i = 0; i < s_texture_count; i++) {
            if (s_textures[i].tex == tex) {
                cols = s_textures[i].cols;
                rows = s_textures[i].rows;
                break;
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform3f(u_grid, (float)cols, 1.0f / (cols * 256.0f), 1.0f / (rows * 256.0f));
}
#else
#define bind_pages(u_grid, tex) glBindTexture(GL_TEXTURE_2D_ARRAY, (tex))
#endif

/* A texture slot's pages as a texture array. */
static GLuint texture_get(int slot)
{
    const u8 *pixels;
    int pages, i;
    struct texture_entry *e;

    if (slot < 0 || slot >= SCENE_TEXTURES || g_scene_textures[slot] == NULL) {
        return 0;
    }
    pixels = g_scene_textures[slot];
    pages = g_scene_texture_pages[slot];
    for (i = 0; i < s_texture_count; i++) {
        if (s_textures[i].pixels == pixels && s_textures[i].pages == pages) {
            return s_textures[i].tex;
        }
    }
    if (s_texture_count == MAX_TEXTURES || pages <= 0) {
        return 0;
    }
    e = &s_textures[s_texture_count++];
    e->pixels = pixels;
    e->pages = pages;
#ifdef IGN_GL_VITA
    e->tex = pages_texture(pixels, pages, &e->cols, &e->rows);
#else
    glGenTextures(1, &e->tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, e->tex);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, 256, 256, pages, 0, GL_RED, GL_UNSIGNED_BYTE,
                 pixels);
#endif
    return e->tex;
}

void gl3d_flush(void)
{
    int i;

    if (!s_ready) {
        return;
    }
    for (i = 0; i < s_texture_count; i++) {
        glDeleteTextures(1, &s_textures[i].tex);
    }
    s_texture_count = 0;
    for (i = 0; i < s_table_count; i++) {
        glDeleteTextures(1, &s_tables[i].tex);
    }
    s_table_count = 0;
    if (s_raster_pages != 0) {
        glDeleteTextures(1, &s_raster_pages);
        s_raster_pages = 0;
    }
    s_mesh_count = 0;
    s_vbo_used = 0;
}

static void target_begin(struct target *t, int w, int h)
{
    if (!s_ready) {
        init();
    }
    if (t->fbo == 0) {
        glGenFramebuffers(1, &t->fbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    s_fbo = t->fbo;
    if (w != t->w || h != t->h) {
        if (t->color != 0) {
            glDeleteTextures(1, &t->color);
            glDeleteRenderbuffers(1, &t->depth);
        }
        glGenTextures(1, &t->color);
        glBindTexture(GL_TEXTURE_2D, t->color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, w, h, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
        glGenRenderbuffers(1, &t->depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t->depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depth);
        t->w = w;
        t->h = h;
    }
    s_target = t;
    s_w = w;
    s_h = h;
    s_ox = s_oy = 0;
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gl3d_begin(int w, int h)
{
    target_begin(&s_targets[0], w, h);
    s_vw = w;
    s_vh = h;
    if (s_readback_size < (size_t)w * h * 2) {
        free(s_readback);
        s_readback_size = (size_t)w * h * 2;
        s_readback = malloc(s_readback_size);
    }
}

void gl3d_view_begin(int w, int h, double vw, double vh)
{
    target_begin(&s_targets[1], w, h);
    s_vw = vw;
    s_vh = vh;
}

/* Fills rows y0..y1 of the view (in its virtual rows), across its whole
 * width, with a palette index, as the original fills the back buffer
 * before drawing into it. */
void gl3d_view_fill(double y0, double y1, int index)
{
    int r0 = (int)(y0 * s_h / s_vh + 0.5), r1 = (int)(y1 * s_h / s_vh + 0.5);

    if (r1 <= r0) {
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, r0, s_w, r1 - r0);
    glClearColor(IGN_INDEX_F(index & 0xff), 1.0f, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0, 0, 0, 0);
    glDisable(GL_SCISSOR_TEST);
}

/* Binds the view again, keeping what is drawn (a second view in the
 * same frame). */
void gl3d_view_resume(void)
{
    struct target *t = &s_targets[1];

    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    s_fbo = t->fbo;
    glViewport(0, 0, t->w, t->h);
    s_target = t;
    s_w = t->w;
    s_h = t->h;
    s_ox = s_oy = 0;
}

/* Limits drawing to the view's columns x0..x1 (its virtual columns);
 * x0 >= x1 lifts the limit. */
void gl3d_view_columns(double x0, double x1)
{
    int c0, c1;

    if (x0 >= x1) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    c0 = (int)(x0 * s_w / s_vw + 0.5);
    c1 = (int)(x1 * s_w / s_vw + 0.5);
    c0 = c0 < 0 ? 0 : c0;
    c1 = c1 > s_w ? s_w : c1;
    glEnable(GL_SCISSOR_TEST);
    glScissor(c0, 0, c1 > c0 ? c1 - c0 : 0, s_h);
}

void gl3d_view_end(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned gl3d_view_texture(void)
{
    return s_targets[1].color;
}

/* Mesh coordinates to clip space for one object. Rows 0 and 1 map to
 * pixels from the frame's top left (GL's row 0 is the screen's first
 * row, so the read back needs no flip); w is Zc + near. */
static void object_matrix(const struct scene *sc, const struct scene_obj *o, double ox,
                          double oy, float out[16])
{
    double view[3][3], rot[3][3], m[3][3], t[3], clip[4][4];
    double n = sc->near_ofs, near_w = 1.0, far_w = 200000.0;
    double ax = -2.0 * sc->focal_x * sc->zoom / s_vw, bx = 2.0 * (ox + s_ox + sc->center_x) / s_vw - 1.0;
    double ay = -2.0 * sc->focal_y / s_vh, by = 2.0 * (oy + s_oy + sc->center_y) / s_vh - 1.0;
    double az = (far_w + near_w) / (far_w - near_w), bz = -2.0 * far_w * near_w / (far_w - near_w);
    double d[3];
    int i, j, k;

    scene_view_rows(sc, view);
    if (o->ax == 0 && o->ay == 0 && o->az == 0) {
        memset(rot, 0, sizeof(rot));
        rot[0][0] = 1;
        rot[1][1] = -1;
        rot[2][2] = 1;
    } else {
        scene_obj_rows(o, rot);
    }
    d[0] = o->x - sc->x;
    d[1] = o->y - sc->y;
    d[2] = o->z - sc->z;
    for (i = 0; i < 3; i++) {
        t[i] = 0;
        for (j = 0; j < 3; j++) {
            m[i][j] = 0;
            for (k = 0; k < 3; k++) {
                m[i][j] += view[i][k] * rot[k][j];
            }
            t[i] += view[i][j] * d[j];
        }
    }
    /* view (m v + t) -> clip */
    for (j = 0; j < 3; j++) {
        clip[0][j] = ax * m[0][j] + bx * m[2][j];
        clip[1][j] = ay * m[1][j] + by * m[2][j];
        clip[2][j] = az * m[2][j];
        clip[3][j] = m[2][j];
    }
    clip[0][3] = ax * t[0] + bx * (t[2] + n);
    clip[1][3] = ay * t[1] + by * (t[2] + n);
    clip[2][3] = az * (t[2] + n) + bz;
    clip[3][3] = t[2] + n;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[j * 4 + i] = (float)clip[i][j];
        }
    }
}

/* Deferred faces: the texel through the table with the pixel under it
 * (a copy in u_dst), or as the main pass draws them. A keyed face
 * leaves colour 0 out either way. */
#ifdef IGN_GL_VITA
static const char *s_dyn_vs =
    "void main(float4 a_pos, float2 a_uv, float3 a_info,\n"
    "          float2 out v_uv : TEXCOORD0, float3 out v_info : TEXCOORD1,\n"
    "          float4 out gl_Position : POSITION) {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *s_dyn_fs =
    CG_PAGES
    "float table_of(sampler2D table, sampler2D dst, float2 dst_size, float4 frag, float c) {\n"
    "    float d = tex2D(dst, (floor(frag.xy) + 0.5) / dst_size).r * 255.0;\n"
    "    float2 at = float2(floor(d + 0.5) + 0.5, floor(c + 0.5) + 0.5) / 256.0;\n"
    "    return tex2D(table, at).r * 255.0;\n"
    "}\n"
    "float4 main(float2 v_uv : TEXCOORD0, float3 v_info : TEXCOORD1, float4 frag : WPOS,\n"
    "            uniform sampler2D u_pages, uniform float3 u_grid, uniform sampler2D u_table,\n"
    "            uniform sampler2D u_dst, uniform float2 u_dst_size,\n"
    "            uniform float u_use_table) : COLOR {\n"
    "    float c = v_info.z > 2.5 ? floor(v_uv.x) : v_info.y;\n"
    "    if (v_info.z < 1.5) {\n"
    "        float2 t = min(floor(frac(v_uv) * 256.0), 255.0);\n"
    "        c = pages_texel(u_pages, u_grid, floor(v_info.x + 0.5), t).r * 255.0;\n"
    "        if (v_info.z > 0.5 && c < 0.5) {\n"
    "            discard;\n"
    "        }\n"
    "    }\n"
    "    if (u_use_table > 0.5) {\n"
    "        c = table_of(u_table, u_dst, u_dst_size, frag, c);\n"
    "    }\n"
    "    return float4((floor(c + 0.5) + 0.25) / 255.0, 1.0, 0.0, 1.0);\n"
    "}\n";
#else
static const char *s_dyn_vs =
    "#version 150\n"
    "in vec4 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec3 a_info;\n"
    "out vec2 v_uv;\n"
    "flat out vec3 v_info;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *s_dyn_fs =
    "#version 150\n"
    "uniform sampler2DArray u_pages;\n"
    "uniform sampler2D u_table;\n"
    "uniform sampler2D u_dst;\n"
    "uniform int u_use_table;\n"
    "in vec2 v_uv;\n"
    "flat in vec3 v_info;\n"
    "out vec2 o_index;\n"
    "void main() {\n"
    "    float c = v_info.z > 2.5 ? floor(v_uv.x) : v_info.y;\n"
    "    if (v_info.z < 1.5) {\n"
    "        c = texture(u_pages, vec3(v_uv, v_info.x)).r * 255.0;\n"
    "        if (v_info.z > 0.5 && c < 0.5) {\n"
    "            discard;\n"
    "        }\n"
    "        if (u_use_table == 1) {\n"
    "            float d = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0).r * 255.0;\n"
    "            c = texelFetch(u_table, ivec2(int(d + 0.5), int(c + 0.5)), 0).r * 255.0;\n"
    "        }\n"
    "    } else if (u_use_table == 1) {\n"
    "        float d = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0).r * 255.0;\n"
    "        c = texelFetch(u_table, ivec2(int(d + 0.5), int(c + 0.5)), 0).r * 255.0;\n"
    "    }\n"
    "    o_index = vec2(c / 255.0, 1.0);\n"
    "}\n";
#endif

static void dyn_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, s_dyn_vs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_dyn_fs);

    s_dyn_program = glCreateProgram();
    glAttachShader(s_dyn_program, vs);
    glAttachShader(s_dyn_program, fs);
    glBindAttribLocation(s_dyn_program, 0, "a_pos");
    glBindAttribLocation(s_dyn_program, 1, "a_uv");
    glBindAttribLocation(s_dyn_program, 2, "a_info");
    glBindFragDataLocation(s_dyn_program, 0, "o_index");
    glLinkProgram(s_dyn_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    s_du_pages = glGetUniformLocation(s_dyn_program, "u_pages");
    s_du_table = glGetUniformLocation(s_dyn_program, "u_table");
    s_du_dst = glGetUniformLocation(s_dyn_program, "u_dst");
    s_du_use_table = glGetUniformLocation(s_dyn_program, "u_use_table");
#ifdef IGN_GL_VITA
    s_du_grid = glGetUniformLocation(s_dyn_program, "u_grid");
    s_du_dst_size = glGetUniformLocation(s_dyn_program, "u_dst_size");
#endif
    glGenVertexArrays(1, &s_dyn_vao);
    glBindVertexArray(s_dyn_vao);
    glGenBuffers(1, &s_dyn_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_dyn_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * DYN_FLOATS * sizeof(float), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, DYN_FLOATS * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, DYN_FLOATS * sizeof(float),
                          (void *)(4 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, DYN_FLOATS * sizeof(float),
                          (void *)(6 * sizeof(float)));
}

void gl3d_set_tables(const u8 *shd, const u8 *fixed)
{
    s_tables_shd = shd;
    s_tables_fixed = fixed;
}

void gl3d_set_dynamic(const s32 *begin, const s32 *end)
{
    int i;

    for (i = 0; i < s_dyn_range_count; i++) {
        if (s_dyn_ranges[i].begin == begin) {
            s_dyn_ranges[i].end = end;
            return;
        }
    }
    if (s_dyn_range_count < 8) {
        s_dyn_ranges[s_dyn_range_count].begin = begin;
        s_dyn_ranges[s_dyn_range_count].end = end;
        s_dyn_range_count++;
    }
}

static int is_dynamic(const s32 *mesh)
{
    int i;

    for (i = 0; i < s_dyn_range_count; i++) {
        if (mesh >= s_dyn_ranges[i].begin && mesh < s_dyn_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

static void clip_of(const float m[16], const s32 *v, float c[4])
{
    int k;

    for (k = 0; k < 4; k++) {
        c[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2] + m[12 + k];
    }
}

static void dyn_vertex(float *o, const float c[4], float u, float v, float page, float colour,
                       float mode)
{
    o[0] = c[0];
    o[1] = c[1];
    o[2] = c[2];
    o[3] = c[3];
    o[4] = u;
    o[5] = v;
    o[6] = page;
    o[7] = colour;
    o[8] = mode;
}

/* A screen box around clip point c, half hx, hy in NDC, kept at c's
 * depth: two triangles. */
static void dyn_box(struct dyn *d, const float c[4], float hx, float hy, const float uv[4],
                    float page, float colour, float mode)
{
    float x0 = c[0] / c[3] - hx, x1 = c[0] / c[3] + hx, y0 = c[1] / c[3] - hy, y1 = c[1] / c[3] + hy;
    float z = c[2] / c[3];
    float p[4][4] = { { x0, y0, z, 1 }, { x1, y0, z, 1 }, { x0, y1, z, 1 }, { x1, y1, z, 1 } };

    dyn_vertex(d->v[0], p[0], uv[0], uv[1], page, colour, mode);
    dyn_vertex(d->v[1], p[1], uv[2], uv[1], page, colour, mode);
    dyn_vertex(d->v[2], p[2], uv[0], uv[3], page, colour, mode);
    dyn_vertex(d->v[3], p[1], uv[2], uv[1], page, colour, mode);
    dyn_vertex(d->v[4], p[3], uv[2], uv[3], page, colour, mode);
    dyn_vertex(d->v[5], p[2], uv[0], uv[3], page, colour, mode);
    d->count = 6;
}

/* An object's faces for the deferred pass: all of a dynamic mesh's, or
 * a static mesh's table faces. Points and sprites are placed as the
 * rasteriser places them on the screen (8.8 sprite sizes of 0x44d0f0
 * and 0x44d230 through 0x455648's box). */
static void collect(const struct scene *sc, const struct scene_obj *o, const float m[16],
                    int all)
{
    const s32 *mesh = o->mesh;
    const u8 *f = (const u8 *)(mesh + 2 + 3 * mesh[0]);
    GLuint tex = 0;
    int i, have_tex = 0;

    for (i = 0; i < mesh[1] && s_dyn_count < MAX_DYN; i++) {
        const s32 *w = (const s32 *)f;
        int type = (s8)f[0], size = face_size(type);
        struct dyn *d = &s_dyns[s_dyn_count];
        float c[3][4];
        int k;

        if (size == 0) {
            break;
        }
        f += size;
        if (!all && type != 19 && type != 23 && type != 1 && type != 2 && type != 7 && type != 8
            && type != 13 && type != 20) {
            continue;
        }
        if (!have_tex) {
            tex = texture_get(o->tex);
            have_tex = 1;
        }
        d->tex = tex;
        d->table = NULL;
        if (type == 1 || type == 2) {
            float h = type == 1 ? 0.5f : 1.0f, uv[4] = { 0, 0, 0, 0 };

            clip_of(m, mesh + 2 + 3 * w[1], c[0]);
            /* The original leaves out what is within 200 of the eye. */
            if (c[0][3] <= 200.0f) {
                continue;
            }
            dyn_box(d, c[0], h * 2.0f / (float)s_vw, h * 2.0f / (float)s_vh, uv, 0,
                    (float)(w[2] & 0xff), MODE_FLAT);
            d->depth = c[0][3];
        } else if (type == 7 || type == 8) {
            double hot_x = (double)(w[4] + w[2]) / 2 - w[2], hot_y = (double)(w[5] + w[3]) / 2 - w[3];
            float uv[4] = { w[2] / 65536.0f, w[3] / 65536.0f, w[4] / 65536.0f, w[5] / 65536.0f };
            s32 sx, sy;

            clip_of(m, mesh + 2 + 3 * w[1], c[0]);
            if (c[0][3] <= 200.0f || tex == 0) {
                continue;
            }
            if (type == 7) {
                double k4 = 4.0 / (s32)c[0][3];

                sx = (s32)(w[7] * k4);
                sy = (s32)(w[8] * k4);
                d->table = s_tables_shd;
            } else {
                double px, py;

                sx = (s32)(0.003952569328248501 * sc->focal_y * w[7]);
                sy = (s32)(sc->focal_y * 0.004820206304829848 * w[8]);
                d->table = s_tables_fixed;
                /* Type 8 sits on a whole pixel. */
                px = floor((c[0][0] / c[0][3] + 1.0) * s_vw / 2);
                py = floor((c[0][1] / c[0][3] + 1.0) * s_vh / 2);
                c[0][0] = (float)((px * 2 / s_vw - 1.0) * c[0][3]);
                c[0][1] = (float)((py * 2 / s_vh - 1.0) * c[0][3]);
            }
            /* hot (8.8 texels) * scale >> 16 is 8.8 pixels. */
            dyn_box(d, c[0], (float)(hot_x * sx / 16777216.0 * 2.0 / s_vw),
                    (float)(hot_y * sy / 16777216.0 * 2.0 / s_vh), uv, (float)((u32)w[6] >> 16), 0,
                    MODE_KEYED);
            d->depth = c[0][3];
        } else if (type == 13) {
            /* A line a pixel wide (0x44d470), its colour running from w[3]
             * to w[4] (8.8), through the .SHD when w[5] is set (the
             * original keeps the table's address there). */
            float a[3], b[3], dx, dy, len, nx, ny;
            float p[4][4];

            clip_of(m, mesh + 2 + 3 * w[1], c[0]);
            clip_of(m, mesh + 2 + 3 * w[2], c[1]);
            if (c[0][3] + c[1][3] <= 400.0f || c[0][3] <= 1.0f || c[1][3] <= 1.0f) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                a[k] = c[0][k] / c[0][3];
                b[k] = c[1][k] / c[1][3];
            }
            dx = (b[0] - a[0]) * (float)s_vw;
            dy = (b[1] - a[1]) * (float)s_vh;
            len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-6f) {
                nx = 1.0f / (float)s_vw;
                ny = 0;
            } else {
                nx = -dy / len / (float)s_vw;
                ny = dx / len / (float)s_vh;
            }
            p[0][0] = a[0] - nx; p[0][1] = a[1] - ny; p[0][2] = a[2]; p[0][3] = 1;
            p[1][0] = a[0] + nx; p[1][1] = a[1] + ny; p[1][2] = a[2]; p[1][3] = 1;
            p[2][0] = b[0] - nx; p[2][1] = b[1] - ny; p[2][2] = b[2]; p[2][3] = 1;
            p[3][0] = b[0] + nx; p[3][1] = b[1] + ny; p[3][2] = b[2]; p[3][3] = 1;
            dyn_vertex(d->v[0], p[0], w[3] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            dyn_vertex(d->v[1], p[1], w[3] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            dyn_vertex(d->v[2], p[2], w[4] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            dyn_vertex(d->v[3], p[1], w[3] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            dyn_vertex(d->v[4], p[3], w[4] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            dyn_vertex(d->v[5], p[2], w[4] / 256.0f, 0, 0, 0, MODE_GRADIENT);
            d->count = 6;
            d->depth = (c[0][3] + c[1][3]) * 0.5f;
            d->table = w[5] != 0 ? s_tables_shd : NULL;
        } else if (size == 44 || type == 15 || type == 16 || type == 20) {
            int flat = type == 15 || type == 16 || type == 20 || type == 21;
            float page = flat ? 0 : (float)((u32)w[10] >> 16);
            float mode = flat ? MODE_FLAT : (type == 18 || type == 22) ? MODE_KEYED : MODE_TEXTURE;
            float colour = flat ? (float)(w[4] & 0xff) : 0;

            for (k = 0; k < 3; k++) {
                clip_of(m, mesh + 2 + 3 * w[1 + k], c[k]);
            }
            if (c[0][3] <= 1.0f && c[1][3] <= 1.0f && c[2][3] <= 1.0f) {
                continue;
            }
            /* Type 20 (0x44cf00): a flat face through the .SHD, left out
             * near the eye (its three depths up to 600). */
            if (type == 20 && c[0][3] + c[1][3] + c[2][3] <= 600.0f) {
                continue;
            }
            if (!flat && tex == 0) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                dyn_vertex(d->v[k], c[k], flat ? 0 : w[4 + 2 * k] / 65536.0f,
                           flat ? 0 : w[5 + 2 * k] / 65536.0f, page, colour, mode);
            }
            d->count = 3;
            d->depth = (c[0][3] + c[1][3] + c[2][3]) / 3.0f;
            if (type == 19 || type == 23 || type == 20) {
                d->table = s_tables_shd;
            }
        } else {
            warn_once(type);
            continue;
        }
        s_dyn_count++;
    }
}

static int dyn_far_first(const void *a, const void *b)
{
    const struct dyn *p = a, *q = b;

    return p->depth < q->depth ? 1 : p->depth > q->depth ? -1 : 0;
}

/* The pixels a face covers, from its clip space vertices. */
static int dyn_rect(const struct dyn *d, int r[4])
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int k;

    for (k = 0; k < d->count; k++) {
        float w = d->v[k][3], x, y;

        if (w <= 0.0f) {
            /* Behind the eye: take the whole frame. */
            x0 = y0 = -1;
            x1 = y1 = 1;
            break;
        }
        x = d->v[k][0] / w;
        y = d->v[k][1] / w;
        x0 = x < x0 ? x : x0;
        x1 = x > x1 ? x : x1;
        y0 = y < y0 ? y : y0;
        y1 = y > y1 ? y : y1;
    }
    r[0] = (int)floor((x0 + 1.0f) * 0.5f * s_w) - 1;
    r[1] = (int)floor((y0 + 1.0f) * 0.5f * s_h) - 1;
    r[2] = (int)ceil((x1 + 1.0f) * 0.5f * s_w) + 1;
    r[3] = (int)ceil((y1 + 1.0f) * 0.5f * s_h) + 1;
    r[0] = r[0] < 0 ? 0 : r[0];
    r[1] = r[1] < 0 ? 0 : r[1];
    r[2] = r[2] > s_w ? s_w : r[2];
    r[3] = r[3] > s_h ? s_h : r[3];
    return r[2] > r[0] && r[3] > r[1];
}

/* Back to front, each through its table over a copy of what is under
 * it. */
static void draw_dyns(void)
{
    int i;

    if (s_dyn_count == 0) {
        return;
    }
    if (s_dyn_program == 0) {
        dyn_init();
    }
    if (s_dyn_dst == 0 || s_dyn_dst_w != s_w || s_dyn_dst_h != s_h) {
        if (s_dyn_dst != 0) {
            ign_forget_texture(s_dyn_dst);
            glDeleteTextures(1, &s_dyn_dst);
        }
        glGenTextures(1, &s_dyn_dst);
        glBindTexture(GL_TEXTURE_2D, s_dyn_dst);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, s_w, s_h, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
        s_dyn_dst_w = s_w;
        s_dyn_dst_h = s_h;
    }
    qsort(s_dyns, (size_t)s_dyn_count, sizeof(s_dyns[0]), dyn_far_first);
    glUseProgram(s_dyn_program);
    glBindVertexArray(s_dyn_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_dyn_vbo);
    glUniform1i(s_du_pages, 0);
    glUniform1i(s_du_table, 1);
    glUniform1i(s_du_dst, 2);
#ifdef IGN_GL_VITA
    glUniform2f(s_du_dst_size, (float)s_w, (float)s_h);
#endif
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    for (i = 0; i < s_dyn_count; i++) {
        const struct dyn *d = &s_dyns[i];
        int use_table = d->table != NULL, r[4];

        if (use_table) {
            if (!dyn_rect(d, r)) {
                continue;
            }
            glActiveTexture(GL_TEXTURE2);
            ign_copy_rect(s_fbo, s_dyn_dst, r[0], r[1], r[2] - r[0], r[3] - r[1]);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, table_get(d->table));
        }
        /* Through a table nothing writes the depth, as the faces show what
         * is behind them; and they are pulled a little toward the eye, as
         * the shadows and skids lie on the ground they shade. */
        glDepthMask(use_table ? GL_FALSE : GL_TRUE);
        if (use_table) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -4.0f);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glActiveTexture(GL_TEXTURE0);
        bind_pages(s_du_grid, d->tex);
        glUniform1i(s_du_use_table, use_table);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)d->count * DYN_FLOATS * sizeof(float), d->v);
        glDrawArrays(GL_TRIANGLES, 0, d->count);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    s_dyn_count = 0;
}

void gl3d_draw(const struct scene *sc, struct scene_obj *const *objs, int count, double ox,
               double oy)
{
    int i;

    glUseProgram(s_program);
    glBindVertexArray(s_vao);
    glUniform1i(s_u_pages, 0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(sc->zoom > 0 ? GL_CW : GL_CCW);
    glActiveTexture(GL_TEXTURE0);
    for (i = 0; i < count; i++) {
        const struct scene_obj *o = objs[i];
        struct mesh_entry *e;
        float m[16];

        if (is_dynamic(o->mesh)) {
            object_matrix(sc, o, ox, oy, m);
            collect(sc, o, m, 1);
            continue;
        }
        e = mesh_get(o->mesh);
        if (e == NULL) {
            continue;
        }
        object_matrix(sc, o, ox, oy, m);
        if (e->deferred) {
            collect(sc, o, m, 0);
        }
        if (e->count == 0) {
            continue;
        }
        glUseProgram(s_program);
        glBindVertexArray(s_vao);
        bind_pages(s_u_grid, texture_get(o->tex));
        glUniformMatrix4fv(s_u_matrix, 1, GL_FALSE, m);
        glDrawArrays(GL_TRIANGLES, e->first, e->count);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    draw_dyns();
}

void gl3d_end(u8 *screen, int pitch)
{
    int x, y;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, s_w, s_h, GL_RG, GL_UNSIGNED_BYTE, s_readback);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (y = 0; y < s_h; y++) {
        const u8 *src = s_readback + (size_t)y * s_w * 2;
        u8 *dst = screen + (size_t)y * pitch;

        for (x = 0; x < s_w; x++) {
            if (src[2 * x + 1] != 0) {
                dst[x] = src[2 * x];
            }
        }
    }
}

/* Raster commands: pixels in, the index out. Modes: 0 the texel, 1 the
 * colour, 2 the texel and 3 the colour through the table with the
 * pixel under them. Vertices go half a pixel left and up, as the
 * original fills a pixel whose bottom right corner is inside. */
#ifdef IGN_GL_VITA
static const char *s_raster_vs =
    "void main(float2 a_pos, float2 a_uv, float3 a_info, uniform float2 u_size,\n"
    "          uniform float2 u_off, float2 out v_uv : TEXCOORD0,\n"
    "          float3 out v_info : TEXCOORD1, float4 out gl_Position : POSITION) {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = float4((a_pos - 0.5 + u_off) / u_size * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *s_raster_fs =
    CG_PAGES
    "float4 main(float2 v_uv : TEXCOORD0, float3 v_info : TEXCOORD1, float4 frag : WPOS,\n"
    "            uniform sampler2D u_pages, uniform float3 u_grid, uniform sampler2D u_table,\n"
    "            uniform sampler2D u_dst, uniform float2 u_dst_size) : COLOR {\n"
    "    float mode = v_info.z;\n"
    "    float a = v_info.y;\n"
    "    if (mode < 0.5 || (mode > 1.5 && mode < 2.5)) {\n"
    "        float2 t = floor(v_uv);\n"
    "        t = t - 256.0 * floor(t / 256.0);\n"
    "        a = pages_texel(u_pages, u_grid, floor(v_info.x + 0.5), t).r * 255.0;\n"
    "    }\n"
    "    if (mode > 1.5) {\n"
    "        float d = tex2D(u_dst, (floor(frag.xy) + 0.5) / u_dst_size).r * 255.0;\n"
    "        float2 at = float2(floor(d + 0.5) + 0.5, floor(a + 0.5) + 0.5) / 256.0;\n"
    "        a = tex2D(u_table, at).r * 255.0;\n"
    "    }\n"
    "    return float4((floor(a + 0.5) + 0.25) / 255.0, 0.0, 0.0, 1.0);\n"
    "}\n";
#else
static const char *s_raster_vs =
    "#version 150\n"
    "uniform vec2 u_size;\n"
    "uniform vec2 u_off;\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec3 a_info;\n"
    "out vec2 v_uv;\n"
    "flat out vec3 v_info;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_info = a_info;\n"
    "    gl_Position = vec4((a_pos - 0.5 + u_off) / u_size * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *s_raster_fs =
    "#version 150\n"
    "uniform sampler2DArray u_pages;\n"
    "uniform sampler2D u_table;\n"
    "uniform sampler2D u_dst;\n"
    "in vec2 v_uv;\n"
    "flat in vec3 v_info;\n"
    "out float o_index;\n"
    "void main() {\n"
    "    float mode = v_info.z;\n"
    "    float a = v_info.y;\n"
    "    if (mode < 0.5 || (mode > 1.5 && mode < 2.5)) {\n"
    "        ivec2 t = ivec2(floor(v_uv)) & 255;\n"
    "        a = texelFetch(u_pages, ivec3(t, int(v_info.x)), 0).r * 255.0;\n"
    "    }\n"
    "    if (mode > 1.5) {\n"
    "        float d = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0).r * 255.0;\n"
    "        a = texelFetch(u_table, ivec2(int(d + 0.5), int(a + 0.5)), 0).r * 255.0;\n"
    "    }\n"
    "    o_index = a / 255.0;\n"
    "}\n";
#endif

static void raster_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, s_raster_vs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_raster_fs);

    s_raster_program = glCreateProgram();
    glAttachShader(s_raster_program, vs);
    glAttachShader(s_raster_program, fs);
    glBindAttribLocation(s_raster_program, 0, "a_pos");
    glBindAttribLocation(s_raster_program, 1, "a_uv");
    glBindAttribLocation(s_raster_program, 2, "a_info");
    glBindFragDataLocation(s_raster_program, 0, "o_index");
    glLinkProgram(s_raster_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    s_ru_size = glGetUniformLocation(s_raster_program, "u_size");
    s_ru_off = glGetUniformLocation(s_raster_program, "u_off");
    s_ru_pages = glGetUniformLocation(s_raster_program, "u_pages");
    s_ru_table = glGetUniformLocation(s_raster_program, "u_table");
    s_ru_dst = glGetUniformLocation(s_raster_program, "u_dst");
#ifdef IGN_GL_VITA
    s_ru_grid = glGetUniformLocation(s_raster_program, "u_grid");
    s_ru_dst_size = glGetUniformLocation(s_raster_program, "u_dst_size");
#endif
    glGenVertexArrays(1, &s_raster_vao);
    glBindVertexArray(s_raster_vao);
    glGenBuffers(1, &s_raster_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_raster_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, RASTER_FLOATS * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, RASTER_FLOATS * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, RASTER_FLOATS * sizeof(float),
                          (void *)(4 * sizeof(float)));
    glGenFramebuffers(1, &s_raster_fbo);
    s_raster_ready = 1;
}

static GLuint r8_texture(int w, int h)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    return t;
}

static GLuint table_get(const u8 *table)
{
    struct table_entry *e;
    int i;

    for (i = 0; i < s_table_count; i++) {
        if (s_tables[i].table == table) {
            return s_tables[i].tex;
        }
    }
    if (s_table_count == MAX_TABLES) {
        return s_tables[0].tex;
    }
    e = &s_tables[s_table_count++];
    e->table = table;
    e->tex = r8_texture(256, 256);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED, GL_UNSIGNED_BYTE, table);
    return e->tex;
}

void gl3d_raster_pages(const u8 *pixels, int count)
{
    if (!s_raster_ready) {
        raster_init();
    }
    if (s_raster_pages != 0) {
        glDeleteTextures(1, &s_raster_pages);
    }
#ifdef IGN_GL_VITA
    s_raster_pages = pages_texture(pixels, count, &s_raster_cols, &s_raster_rows);
#else
    glGenTextures(1, &s_raster_pages);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_raster_pages);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, 256, 256, count, 0, GL_RED, GL_UNSIGNED_BYTE,
                 pixels);
#endif
}

static int cmd_mode(int type)
{
    switch (type) {
    case RASTER_TEXTURE: return 0;
    case RASTER_FLAT: return 1;
    case RASTER_TEXTURE_TABLE: return 2;
    default: return 3;
    }
}

void gl3d_raster(u8 *target, int w, int h, int pitch, const struct raster_cmd *cmds, int count)
{
    int i, k, run, pw, ph;
    int gpu = canvas_is_gpu(target);

    if (!s_raster_ready) {
        raster_init();
    }
    if (!gpu && (w != s_raster_w || h != s_raster_h)) {
        if (s_raster_color != 0) {
            ign_forget_texture(s_raster_dst);
            glDeleteTextures(1, &s_raster_color);
            glDeleteTextures(1, &s_raster_dst);
        }
        s_raster_color = r8_texture(w, h);
        s_raster_dst = r8_texture(w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, s_raster_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s_raster_color, 0);
        s_raster_w = w;
        s_raster_h = h;
    }
    if (count * 3 > s_raster_cap) {
        s_raster_cap = count * 3;
        free(s_raster_verts);
        s_raster_verts = malloc((size_t)s_raster_cap * RASTER_FLOATS * sizeof(float));
        glBindBuffer(GL_ARRAY_BUFFER, s_raster_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s_raster_cap * RASTER_FLOATS * sizeof(float),
                     NULL, GL_STREAM_DRAW);
    }
    for (i = 0; i < count; i++) {
        float *o = s_raster_verts + i * 3 * RASTER_FLOATS;

        for (k = 0; k < 3; k++, o += RASTER_FLOATS) {
            o[0] = cmds[i].x[k];
            o[1] = cmds[i].y[k];
            o[2] = cmds[i].u[k];
            o[3] = cmds[i].v[k];
            o[4] = (float)cmds[i].page;
            o[5] = cmds[i].colour;
            o[6] = (float)cmd_mode(cmds[i].type);
            o[7] = 0;
        }
    }

    if (gpu) {
        /* Straight into the surface, at its resolution. */
        canvas_bind(target, &pw, &ph, 0);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, s_raster_fbo);
        glViewport(0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, s_raster_color);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, target);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glUseProgram(s_raster_program);
    glBindVertexArray(s_raster_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_raster_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)count * 3 * RASTER_FLOATS * sizeof(float),
                    s_raster_verts);
    {
        /* A surface with NATIVE's margins spans more than w x h. */
        float mx = 0, my = 0;

        if (gpu) {
            canvas_extent(target, &mx, &my);
        }
        glUniform2f(s_ru_size, w + 2 * mx, h + 2 * my);
        glUniform2f(s_ru_off, mx, my);
    }
    glUniform1i(s_ru_pages, 0);
    glUniform1i(s_ru_table, 1);
    glUniform1i(s_ru_dst, 2);
#ifdef IGN_GL_VITA
    glUniform2f(s_ru_dst_size, (float)(gpu ? pw : w), (float)(gpu ? ph : h));
#endif
    glActiveTexture(GL_TEXTURE0);
    bind_pages(s_ru_grid, s_raster_pages);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    /* Opaque commands go in runs; each translucent one reads what is
     * under it, so the frame is copied before it. */
    for (i = 0; i < count; i += run) {
        if (cmds[i].type == RASTER_TEXTURE || cmds[i].type == RASTER_FLAT) {
            for (run = 1; i + run < count && (cmds[i + run].type == RASTER_TEXTURE
                                              || cmds[i + run].type == RASTER_FLAT); run++) {
            }
        } else {
            run = 1;
            if (gpu) {
                const struct raster_cmd *c = &cmds[i];
                float x0 = c->x[0], x1 = c->x[0], y0 = c->y[0], y1 = c->y[0];
                GLuint copy;

                for (k = 1; k < 3; k++) {
                    x0 = c->x[k] < x0 ? c->x[k] : x0;
                    x1 = c->x[k] > x1 ? c->x[k] : x1;
                    y0 = c->y[k] < y0 ? c->y[k] : y0;
                    y1 = c->y[k] > y1 ? c->y[k] : y1;
                }
                copy = canvas_snapshot_rect(target, x0, y0, x1, y1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, copy);
            } else {
                glActiveTexture(GL_TEXTURE2);
                ign_copy_rect(s_raster_fbo, s_raster_dst, 0, 0, w, h);
            }
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, table_get(cmds[i].table));
            glActiveTexture(GL_TEXTURE0);
        }
        glDrawArrays(GL_TRIANGLES, i * 3, run * 3);
    }

    if (!gpu) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
#ifdef IGN_GL_VITA
        /* No GL_PACK_ROW_LENGTH: read whole rows, then spread them. */
        if (pitch == w) {
            glReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, target);
        } else {
            u8 *rows = malloc((size_t)w * h);

            if (rows != NULL) {
                glReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, rows);
                for (k = 0; k < h; k++) {
                    memcpy(target + (size_t)k * pitch, rows + (size_t)k * w, (size_t)w);
                }
                free(rows);
            }
        }
#else
        glPixelStorei(GL_PACK_ROW_LENGTH, pitch);
        glReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, target);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#endif
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int gl3d_surface_begin(u8 *key, int lw, int lh)
{
    int pw, ph;

    if (!s_ready) {
        init();
    }
    s_fbo = canvas_bind(key, &pw, &ph, 1);
    if (s_fbo == 0) {
        return 0;
    }
    {
        float mx, my;

        canvas_extent(key, &mx, &my);
        s_ox = mx;
        s_oy = my;
        s_vw = lw + 2.0 * mx;
        s_vh = lh + 2.0 * my;
    }
    s_target = NULL;
    s_w = pw;
    s_h = ph;
    return 1;
}

void gl3d_surface_end(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
