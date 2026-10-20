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

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    MODE_FLAT = 2       /* the face's colour */
};

struct mesh_entry {
    const s32 *mesh;
    GLint first;
    GLsizei count;
};

struct texture_entry {
    const u8 *pixels;
    int pages;
    GLuint tex;
};

static int s_ready;
static GLuint s_program, s_vao, s_vbo;
static GLint s_u_matrix, s_u_pages;
static GLuint s_fbo, s_color, s_depth;
static int s_w, s_h;
static struct mesh_entry s_meshes[MAX_MESHES];
static int s_mesh_count;
static GLint s_vbo_used, s_vbo_size;
static struct texture_entry s_textures[MAX_TEXTURES];
static int s_texture_count;
static u8 *s_readback;

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
    glGenFramebuffers(1, &s_fbo);
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

/* Turns a mesh's faces into triangles, cached by the mesh's address. */
static struct mesh_entry *mesh_get(const s32 *mesh)
{
    struct mesh_entry *e;
    const u8 *f;
    float *verts;
    int i, n = 0;

    for (i = 0; i < s_mesh_count; i++) {
        if (s_meshes[i].mesh == mesh) {
            return &s_meshes[i];
        }
    }
    if (s_mesh_count == MAX_MESHES) {
        return NULL;
    }
    verts = malloc((size_t)mesh[1] * 3 * FLOATS * sizeof(float));
    f = (const u8 *)(mesh + 2 + 3 * mesh[0]);
    for (i = 0; i < mesh[1]; i++) {
        const s32 *w = (const s32 *)f;
        int type = (s8)f[0];
        int size = face_size(type);
        float *o = verts + n * FLOATS;

        if (size == 0) {
            break;
        }
        if (size == 44 && type != 19 && type != 22 && type != 23) {
            float page = (float)((u32)w[10] >> 16);
            float mode = type == 18 ? MODE_KEYED : MODE_TEXTURE;
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
    s_vbo_used += n;
    return e;
}

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
    glGenTextures(1, &e->tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, e->tex);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, 256, 256, pages, 0, GL_RED, GL_UNSIGNED_BYTE,
                 pixels);
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
    s_mesh_count = 0;
    s_vbo_used = 0;
}

void gl3d_begin(int w, int h)
{
    if (!s_ready) {
        init();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    if (w != s_w || h != s_h) {
        if (s_color != 0) {
            glDeleteTextures(1, &s_color);
            glDeleteRenderbuffers(1, &s_depth);
        }
        glGenTextures(1, &s_color);
        glBindTexture(GL_TEXTURE_2D, s_color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, w, h, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_color, 0);
        glGenRenderbuffers(1, &s_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, s_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_depth);
        free(s_readback);
        s_readback = malloc((size_t)w * h * 2);
        s_w = w;
        s_h = h;
    }
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Mesh coordinates to clip space for one object. Rows 0 and 1 map to
 * pixels from the frame's top left (GL's row 0 is the screen's first
 * row, so the read back needs no flip); w is Zc + near. */
static void object_matrix(const struct scene *sc, const struct scene_obj *o, int ox, int oy,
                          float out[16])
{
    double view[3][3], rot[3][3], m[3][3], t[3], clip[4][4];
    double n = sc->near_ofs, near_w = 1.0, far_w = 200000.0;
    double ax = -2.0 * sc->focal_x * sc->zoom / s_w, bx = 2.0 * (ox + sc->center_x) / s_w - 1.0;
    double ay = -2.0 * sc->focal_y / s_h, by = 2.0 * (oy + sc->center_y) / s_h - 1.0;
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

void gl3d_draw(const struct scene *sc, struct scene_obj *const *objs, int count, int ox, int oy)
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
        struct mesh_entry *e = mesh_get(o->mesh);
        float m[16];

        if (e == NULL || e->count == 0) {
            continue;
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_get(o->tex));
        object_matrix(sc, o, ox, oy, m);
        glUniformMatrix4fv(s_u_matrix, 1, GL_FALSE, m);
        glDrawArrays(GL_TRIANGLES, e->first, e->count);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
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
