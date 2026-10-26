/*
 * Split DYN programs: ordinary draws use a plain fragment shader while
 * table draws keep the coherent framebuffer-fetch program. Everything
 * goes through the real rnd_gl.c entry points and a recording GLES
 * surface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLES3/gl3.h"
#include "rnd.h"

#ifndef TEST_EXPECT_SPLIT
#define TEST_EXPECT_SPLIT 1
#endif

enum { MAX_SHADERS = 512, MAX_PROGRAMS = 256, SOURCE_CAP = 16384 };

struct shader_rec {
    int used;
    int deleted;
    unsigned generation;
    int failed;
    int dyn;
    int fetch;
    GLenum type;
    char source[SOURCE_CAP];
};

struct program_rec {
    int used;
    int deleted;
    unsigned generation;
    int failed;
    int dyn;
    int fetch;
    GLuint vs, fs;
    GLint use_table_loc;
    GLint use_table_value;
    int use_table_set;
};

struct draw_rec {
    GLuint program, vao, buffer;
    int table;
    GLint table_loc;
    GLsizeiptr stream_bytes;
};

static struct shader_rec s_shaders[MAX_SHADERS];
static struct program_rec s_programs[MAX_PROGRAMS];
static struct draw_rec s_draws[64];
static int s_shader_count, s_program_count, s_draw_count;
static unsigned s_generation;
static int s_shaders_created, s_programs_created;
static int s_shaders_deleted, s_programs_deleted;
static int s_vao_deleted[512], s_vbo_deleted[512];
static int s_double_deletes;
static int s_fail_fetch_compile, s_fail_fetch_link;
static int s_fail_plain_compile, s_fail_plain_link;
static int s_fail_plain_vs, s_fail_vertex_compile, s_fail_raster;
static const char *const *s_extensions;
static int s_extension_count;
static GLuint s_current_program, s_current_vao, s_array_buffer;
static GLuint s_vao_next, s_vbo_next;
static int s_vaos_created, s_vbos_created;
static int s_dyn_vs_seen;
static int s_absent_use_table;  /* 0 none, 1 plain, 2 fetch, 3 both */
static int s_vao_stride[512], s_vao_attrs[512];
static GLint s_last_uniform_program, s_last_uniform_loc;
static int s_last_uniform_kind;
static int s_stream_calls;
static GLsizeiptr s_stream_bytes;
static int s_failures;

enum { UNIFORM_OTHER, UNIFORM_INT, UNIFORM_MATRIX };

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

static int shader_index(GLuint shader)
{
    return shader >= 1000 && shader < 1000 + MAX_SHADERS ? (int)(shader - 1000) : -1;
}

static int program_index(GLuint program)
{
    return program >= 5000 && program < 5000 + MAX_PROGRAMS ? (int)(program - 5000) : -1;
}

/* Lifetime lookup: records stay around after deletion so a second delete
 * can be detected, while program_for() keeps answering only for live
 * programs. */
static struct program_rec *program_record(GLuint program)
{
    int i = program_index(program);

    return i >= 0 && s_programs[i].used ? &s_programs[i] : NULL;
}

static struct program_rec *program_for(GLuint program)
{
    struct program_rec *p = program_record(program);

    return p != NULL && p->generation == s_generation && !p->deleted ? p : NULL;
}

static struct shader_rec *shader_record(GLuint shader)
{
    int i = shader_index(shader);

    return i >= 0 && s_shaders[i].used ? &s_shaders[i] : NULL;
}

/* Every shader and program this test created was released exactly once. */
static int all_resources_deleted_once(void)
{
    int i;

    for (i = 0; i < s_shader_count; i++) {
        if (s_shaders[i].generation == s_generation &&
            (!s_shaders[i].used || !s_shaders[i].deleted)) {
            return 0;
        }
    }
    for (i = 0; i < s_program_count; i++) {
        if (s_programs[i].generation == s_generation &&
            (!s_programs[i].used || !s_programs[i].deleted)) {
            return 0;
        }
    }
    return 1;
}

static void fake_reset(void)
{
    memset(s_draws, 0, sizeof(s_draws));
    memset(s_vao_deleted, 0, sizeof(s_vao_deleted));
    memset(s_vbo_deleted, 0, sizeof(s_vbo_deleted));
    memset(s_vao_stride, 0, sizeof(s_vao_stride));
    memset(s_vao_attrs, 0, sizeof(s_vao_attrs));
    s_generation++;
    s_draw_count = 0;
    s_shaders_created = 0;
    s_programs_created = 0;
    s_shaders_deleted = 0;
    s_programs_deleted = 0;
    s_double_deletes = 0;
    s_fail_fetch_compile = 0;
    s_fail_fetch_link = 0;
    s_fail_plain_compile = 0;
    s_fail_plain_link = 0;
    s_fail_plain_vs = 0;
    s_fail_vertex_compile = 0;
    s_fail_raster = 0;
    s_extensions = NULL;
    s_extension_count = 0;
    s_current_program = 0;
    s_current_vao = 0;
    s_array_buffer = 0;
    s_vao_next = 0;
    s_vbo_next = 0;
    s_vaos_created = 0;
    s_vbos_created = 0;
    s_dyn_vs_seen = 0;
    s_absent_use_table = 0;
    s_last_uniform_program = 0;
    s_last_uniform_loc = 0;
    s_last_uniform_kind = UNIFORM_OTHER;
    s_stream_calls = 0;
    s_stream_bytes = 0;
}

static void extensions_set(const char *const *extensions, int count)
{
    s_extensions = extensions;
    s_extension_count = count;
}

/* ------------------------------------------------------------ GL stubs */

GLuint glCreateShader(GLenum type)
{
    GLuint shader;

    if (s_shader_count == MAX_SHADERS) {
        return 0;
    }
    shader = 1000u + (GLuint)s_shader_count;
    s_shaders[s_shader_count].used = 1;
    s_shaders[s_shader_count].deleted = 0;
    s_shaders[s_shader_count].generation = s_generation;
    s_shaders[s_shader_count].type = type;
    s_shader_count++;
    s_shaders_created++;
    return shader;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings,
                    const GLint *length)
{
    struct shader_rec *r;
    size_t used = 0;
    int i;

    r = shader_record(shader);
    if (r == NULL || r->generation != s_generation) {
        return;
    }
    r->source[0] = '\0';
    for (i = 0; i < count; i++) {
        size_t n;

        if (strings[i] == NULL) {
            continue;
        }
        n = length != NULL && length[i] >= 0 ? (size_t)length[i] : strlen(strings[i]);
        if (used + n >= sizeof(r->source)) {
            n = sizeof(r->source) - 1 - used;
        }
        memcpy(r->source + used, strings[i], n);
        used += n;
        r->source[used] = '\0';
    }
    r->fetch = strstr(r->source, "GL_EXT_shader_framebuffer_fetch") != NULL;
    r->dyn = strstr(r->source, "v_info.z > 2.5") != NULL;
    if (r->type == GL_VERTEX_SHADER && strstr(r->source, "gl_Position = a_pos;") != NULL) {
        s_dyn_vs_seen++;
    }
    r->failed = (r->fetch && s_fail_fetch_compile) ||
                (r->type == GL_VERTEX_SHADER && s_fail_vertex_compile) ||
                (r->dyn && !r->fetch && s_fail_plain_compile) ||
                (r->type == GL_VERTEX_SHADER && s_fail_plain_vs && s_dyn_vs_seen == 2 &&
                 strstr(r->source, "gl_Position = a_pos;") != NULL) ||
                (s_fail_raster && strstr(r->source, "o_index = a / 255.0;") != NULL);
}

void glCompileShader(GLuint shader)
{
    (void)shader;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    struct shader_rec *r = shader_record(shader);

    if (pname != GL_COMPILE_STATUS || r == NULL || r->generation != s_generation) {
        *params = 0;
        return;
    }
    *params = r->failed ? GL_FALSE : GL_TRUE;
}

void glGetShaderInfoLog(GLuint shader, GLsizei buf_size, GLsizei *length, GLchar *info_log)
{
    static const char log[] = "test shader failure";
    size_t n;

    (void)shader;
    n = buf_size > (GLsizei)sizeof(log) ? sizeof(log) - 1 : (size_t)buf_size - 1;
    if (buf_size <= 0) {
        if (length != NULL) {
            *length = 0;
        }
        return;
    }
    memcpy(info_log, log, n);
    info_log[n] = '\0';
    if (length != NULL) {
        *length = (GLsizei)n;
    }
}

void glDeleteShader(GLuint shader)
{
    struct shader_rec *r = shader_record(shader);

    if (r == NULL || r->generation != s_generation) {
        return;
    }
    if (r->deleted) {
        s_double_deletes++;
        return;
    }
    r->deleted = 1;
    s_shaders_deleted++;
}

GLuint glCreateProgram(void)
{
    GLuint program;

    if (s_program_count == MAX_PROGRAMS) {
        return 0;
    }
    program = 5000u + (GLuint)s_program_count;
    s_programs[s_program_count].used = 1;
    s_programs[s_program_count].deleted = 0;
    s_programs[s_program_count].generation = s_generation;
    s_programs[s_program_count].use_table_loc = -2;
    s_program_count++;
    s_programs_created++;
    return program;
}

void glAttachShader(GLuint program, GLuint shader)
{
    struct program_rec *p = program_for(program);
    struct shader_rec *r = shader_record(shader);

    if (p == NULL || r == NULL || r->generation != s_generation) {
        return;
    }
    if (r->fetch) {
        p->fetch = 1;
    }
    if (r->dyn) {
        p->dyn = 1;
    }
    if (r->failed) {
        p->failed = 1;
    }
    if (p->vs == 0) {
        p->vs = shader;
    } else {
        p->fs = shader;
    }
}

void glLinkProgram(GLuint program)
{
    struct program_rec *p = program_for(program);

    if (p != NULL && ((p->fetch && s_fail_fetch_link) ||
                      (p->dyn && !p->fetch && s_fail_plain_link))) {
        p->failed = 1;
    }
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    struct program_rec *p = program_for(program);

    if (pname != GL_LINK_STATUS || p == NULL) {
        *params = 0;
        return;
    }
    *params = p->failed ? GL_FALSE : GL_TRUE;
}

void glDeleteProgram(GLuint program)
{
    struct program_rec *p = program_record(program);

    if (p == NULL || p->generation != s_generation) {
        return;
    }
    if (p->deleted) {
        s_double_deletes++;
        return;
    }
    p->deleted = 1;
    s_programs_deleted++;
}

void glGetIntegerv(GLenum pname, GLint *data)
{
    if (pname == GL_NUM_EXTENSIONS) {
        *data = s_extension_count;
        return;
    }
    *data = 0;
}

const GLubyte *glGetStringi(GLenum name, GLuint index)
{
    if (name != GL_EXTENSIONS || (int)index >= s_extension_count) {
        return NULL;
    }
    return (const GLubyte *)s_extensions[index];
}

GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
    struct program_rec *p = program_for(program);
    GLint loc;
    int i;

    loc = 1 + program_index(program) * 100;
    for (i = 0; name[i] != '\0'; i++) {
        loc += (unsigned char)name[i];
    }
    if (p != NULL && strcmp(name, "u_use_table") == 0) {
        int absent = (s_absent_use_table == 1 && p->dyn && !p->fetch) ||
                     (s_absent_use_table == 2 && p->dyn && p->fetch) ||
                     (s_absent_use_table == 3 && p->dyn);

        if (absent) {
            loc = -1;
        }
        p->use_table_loc = loc;
    }
    return loc;
}

void glGenVertexArrays(GLsizei n, GLuint *arrays)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        arrays[i] = 100u + s_vao_next++;
        s_vaos_created++;
    }
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        buffers[i] = 200u + s_vbo_next++;
        s_vbos_created++;
    }
}

#define VOID0(name) void name(void) {}
#define VOID1(name, a) void name(a x) { (void)x; }
#define VOID2(name, a, b) void name(a x, b y) { (void)x; (void)y; }
#define VOID3(name, a, b, c) void name(a x, b y, c z) { (void)x; (void)y; (void)z; }

VOID1(glActiveTexture, GLenum)
VOID2(glBindFramebuffer, GLenum, GLuint)
VOID2(glBindRenderbuffer, GLenum, GLuint)
VOID2(glBindTexture, GLenum, GLuint)
VOID1(glDepthFunc, GLenum)
VOID1(glDepthMask, GLboolean)
VOID1(glDisable, GLenum)
VOID1(glEnable, GLenum)
VOID1(glEnableVertexAttribArray, GLuint)
VOID1(glClear, GLbitfield)
VOID1(glClearDepthf, GLfloat)
VOID2(glPolygonOffset, GLfloat, GLfloat)
VOID3(glTexParameteri, GLenum, GLenum, GLint)

void glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    (void)v0;
    (void)v1;
    s_last_uniform_program = (GLint)s_current_program;
    s_last_uniform_loc = location;
    s_last_uniform_kind = UNIFORM_OTHER;
}

void glBindVertexArray(GLuint array)
{
    s_current_vao = array;
}

void glUseProgram(GLuint program)
{
    s_current_program = program;
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER) {
        s_array_buffer = buffer;
    }
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{ (void)program; (void)index; (void)name; }
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{ (void)target; (void)size; (void)data; (void)usage; }
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    (void)target;
    (void)offset;
    (void)data;
    s_stream_calls++;
    s_stream_bytes = size;
}
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{ (void)red; (void)green; (void)blue; (void)alpha; }
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height)
{ (void)target; (void)level; (void)xoffset; (void)yoffset; (void)x; (void)y; (void)width; (void)height; }
void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        if (buffers[i] < 512) {
            if (s_vbo_deleted[buffers[i]]++ != 0) {
                s_double_deletes++;
            }
        }
    }
}
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { (void)n; (void)framebuffers; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) { (void)n; (void)renderbuffers; }
void glDeleteTextures(GLsizei n, const GLuint *textures) { (void)n; (void)textures; }
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        if (arrays[i] < 512) {
            if (s_vao_deleted[arrays[i]]++ != 0) {
                s_double_deletes++;
            }
        }
    }
}
void glBlitFramebuffer(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f,
                       GLint g, GLint h, GLbitfield mask, GLenum filter)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)mask; (void)filter; }
void glCullFace(GLenum mode) { (void)mode; }
void glFrontFace(GLenum mode) { (void)mode; }
void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget, GLuint renderbuffer)
{ (void)target; (void)attachment; (void)renderbuffertarget; (void)renderbuffer; }
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level)
{ (void)target; (void)attachment; (void)textarget; (void)texture; (void)level; }
void glGenFramebuffers(GLsizei n, GLuint *framebuffers)
{ GLsizei i; for (i = 0; i < n; i++) framebuffers[i] = 300u + (GLuint)i; }
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{ GLsizei i; for (i = 0; i < n; i++) renderbuffers[i] = 400u + (GLuint)i; }
void glGenTextures(GLsizei n, GLuint *textures)
{ GLsizei i; for (i = 0; i < n; i++) textures[i] = 500u + (GLuint)i; }
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                  GLenum type, void *pixels)
{ (void)x; (void)y; (void)width; (void)height; (void)format; (void)type; (void)pixels; }
void glPixelStorei(GLenum pname, GLint param) { (void)pname; (void)param; }
void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width,
                           GLsizei height)
{ (void)target; (void)internalformat; (void)width; (void)height; }
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{ (void)x; (void)y; (void)width; (void)height; }
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type,
                  const void *pixels)
{ (void)target; (void)level; (void)internalformat; (void)width; (void)height;
  (void)border; (void)format; (void)type; (void)pixels; }
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLsizei height, GLsizei depth, GLint border, GLenum format,
                  GLenum type, const void *pixels)
{ (void)target; (void)level; (void)internalformat; (void)width; (void)height;
  (void)depth; (void)border; (void)format; (void)type; (void)pixels; }
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels)
{ (void)target; (void)level; (void)xoffset; (void)yoffset; (void)width; (void)height;
  (void)format; (void)type; (void)pixels; }
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;
    s_last_uniform_program = (GLint)s_current_program;
    s_last_uniform_loc = location;
    s_last_uniform_kind = UNIFORM_OTHER;
}
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat *value)
{
    (void)count;
    (void)transpose;
    (void)value;
    s_last_uniform_program = (GLint)s_current_program;
    s_last_uniform_loc = location;
    s_last_uniform_kind = UNIFORM_MATRIX;
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer)
{
    (void)index;
    (void)size;
    (void)type;
    (void)normalized;
    (void)pointer;
    if (s_current_vao < 512) {
        s_vao_stride[s_current_vao] = stride;
        s_vao_attrs[s_current_vao]++;
    }
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{ (void)x; (void)y; (void)width; (void)height; }

void glUniform1i(GLint location, GLint v0)
{
    struct program_rec *p = program_for(s_current_program);

    s_last_uniform_program = (GLint)s_current_program;
    s_last_uniform_loc = location;
    s_last_uniform_kind = UNIFORM_INT;
    if (p != NULL && p->use_table_loc == location) {
        p->use_table_value = v0;
        p->use_table_set = 1;
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    struct draw_rec *d;

    (void)mode;
    (void)first;
    (void)count;
    if (s_draw_count >= (int)(sizeof(s_draws) / sizeof(s_draws[0]))) {
        return;
    }
    d = &s_draws[s_draw_count++];
    d->program = s_current_program;
    d->vao = s_current_vao;
    d->buffer = s_array_buffer;
    d->table_loc = s_last_uniform_program == (GLint)s_current_program
                       ? s_last_uniform_loc : -2;
    d->stream_bytes = s_stream_bytes;
    d->table = program_for(s_current_program) != NULL
                   ? program_for(s_current_program)->use_table_value : -1;
}

/* --------------------------------------------------------------- cases */

static const char *fs_source(GLuint program)
{
    struct program_rec *p = program_for(program);
    int i;

    if (p == NULL || p->fs == 0) {
        return NULL;
    }
    i = shader_index(p->fs);
    return i >= 0 && s_shaders[i].used ? s_shaders[i].source : NULL;
}

static int is_plain_dyn(const char *src)
{
    return src != NULL && strstr(src, "v_info.z > 2.5") != NULL &&
           strstr(src, "GL_EXT_shader_framebuffer_fetch") == NULL &&
           strstr(src, "#define IGN_FRAMEBUFFER_FETCH 1") == NULL &&
           strstr(src, "out vec2 o_index;") != NULL;
}

static int is_fetch_dyn(const char *src)
{
    return src != NULL &&
           strstr(src, "GL_EXT_shader_framebuffer_fetch") != NULL &&
           strstr(src, "inout vec2 o_index;") != NULL;
}

static void draw_dyn(int table)
{
    float u = (float)table;
    float verts[18] = { 0 };

    rnd_uniform(RND_U_USE_TABLE, &u);
    rnd_stream(verts, 2);
    rnd_draw(RND_TRIANGLES, 0, 2);
}

#if TEST_EXPECT_SPLIT
static const char *shader_source(GLuint shader)
{
    int i = shader_index(shader);

    return i >= 0 && s_shaders[i].used ? s_shaders[i].source : NULL;
}

/* Deliberately delete each kind of resource twice and leak one program, so
 * the detectors themselves are shown to fail before normal lifecycle runs. */
static void test_lifetime_detectors(void)
{
    static const GLchar text[] = "void main() {}\n";
    const GLchar *parts[1];
    int started = s_failures;
    GLuint program, shader;

    fake_reset();
    program = glCreateProgram();
    CHECK(program != 0);
    glDeleteProgram(program);
    CHECK(s_double_deletes == 0);
    glDeleteProgram(program);
    CHECK(s_double_deletes == 1);
    CHECK(s_programs_deleted == 1);
    CHECK(all_resources_deleted_once());

    fake_reset();
    shader = glCreateShader(GL_FRAGMENT_SHADER);
    CHECK(shader != 0);
    parts[0] = text;
    glShaderSource(shader, 1, parts, NULL);
    CHECK(shader_source(shader) != NULL);
    glDeleteShader(shader);
    CHECK(s_double_deletes == 0);
    glDeleteShader(shader);
    CHECK(s_double_deletes == 1);
    CHECK(s_shaders_deleted == 1);
    CHECK(shader_source(shader) != NULL &&
          strcmp(shader_source(shader), text) == 0);
    CHECK(all_resources_deleted_once());

    fake_reset();
    program = glCreateProgram();
    CHECK(program != 0);
    CHECK(!all_resources_deleted_once());
    glDeleteProgram(program);
    CHECK(all_resources_deleted_once());

    /* Intentional probe errors do not belong to the cases that follow. */
    fake_reset();
    if (s_failures != started) {
        fprintf(stderr, "lifetime detectors failed\n");
    } else {
        puts("lifetime detectors               ok");
    }
}
#endif

static GLint loc_for(GLuint program, const char *name)
{
    GLint loc = 1 + program_index(program) * 100;
    int i;

    for (i = 0; name[i] != '\0'; i++) {
        loc += (unsigned char)name[i];
    }
    return loc;
}

static int is_dyn_source(const char *src)
{
    return src != NULL && strstr(src, "v_info.z > 2.5") != NULL;
}

static void check_init_failure(const char *label)
{
    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    rnd_shutdown();
    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    CHECK(s_programs_created == s_programs_deleted);
    CHECK(s_shaders_created == s_shaders_deleted);
    CHECK(s_double_deletes == 0);
    CHECK(all_resources_deleted_once());
    if (s_failures == 0) {
        printf("%-34s ok\n", label);
    }
}

static void run_dyn_case(const char *label, const char *const *extensions, int extension_count,
                         int fail_fetch_compile, int fail_fetch_link,
                         int fail_plain_compile, int fail_plain_link, int fail_plain_vs,
                         int fail_vertex, int fail_raster, int absent,
                         int expect_init, int expect_fetch, int expect_split)
{
    int started = s_failures;
    int i;

    fake_reset();
    extensions_set(extensions, extension_count);
    s_fail_fetch_compile = fail_fetch_compile;
    s_fail_fetch_link = fail_fetch_link;
    s_fail_plain_compile = fail_plain_compile;
    s_fail_plain_link = fail_plain_link;
    s_fail_plain_vs = fail_plain_vs;
    s_fail_vertex_compile = fail_vertex;
    s_fail_raster = fail_raster;
    s_absent_use_table = absent;

    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    CHECK((rnd_init() != 0) == expect_init);
    if (!expect_init) {
        check_init_failure(label);
        if (s_failures != started) {
            fprintf(stderr, "case failed: %s\n", label);
        }
        return;
    }
    CHECK(rnd_dyn_framebuffer_fetch() == expect_fetch);

    rnd_use(RND_DYN);
    draw_dyn(0);
    draw_dyn(1);
    draw_dyn(0);
    draw_dyn(1);

    CHECK(s_draw_count == 4);
    if (s_draw_count == 4) {
        for (i = 0; i < 4; i++) {
            const char *src = fs_source(s_draws[i].program);

            CHECK(is_dyn_source(src));
            if (expect_split) {
                CHECK((i & 1) == 0 ? is_plain_dyn(src) : is_fetch_dyn(src));
            } else if (expect_fetch) {
                CHECK(is_fetch_dyn(src));
            } else {
                CHECK(is_plain_dyn(src));
            }
            CHECK(s_draws[i].table == (i & 1));
            CHECK(s_draws[i].table_loc == program_for(s_draws[i].program)->use_table_loc);
            CHECK(s_draws[i].vao == s_draws[0].vao);
            CHECK(s_draws[i].buffer == s_draws[0].buffer);
            CHECK(s_vao_stride[s_draws[i].vao] == 9 * (int)sizeof(float));
            CHECK(s_vao_attrs[s_draws[i].vao] == 3);
        }
        CHECK(s_vaos_created == RND_PROGRAMS);
        CHECK(s_vbos_created == 4);
        CHECK(s_stream_calls == 4);
        CHECK(s_stream_bytes == 2 * 9 * (GLsizeiptr)sizeof(float));
    }

    /* Non-DYN programs keep their own cached locations. */
    rnd_use(RND_MESH);
    {
        float m[16] = { 0 };

        CHECK(!is_dyn_source(fs_source(s_current_program)));
        rnd_uniform(RND_U_MATRIX, m);
        CHECK(s_last_uniform_kind == UNIFORM_MATRIX);
        CHECK(s_last_uniform_program == (GLint)s_current_program);
        CHECK(s_last_uniform_loc == loc_for(s_current_program, "u_matrix"));
    }
    rnd_use(RND_RASTER);
    {
        float mode = 0.0f;

        rnd_uniform(RND_U_MODE, &mode);
        CHECK(s_last_uniform_kind == UNIFORM_INT);
        CHECK(s_last_uniform_program == (GLint)s_current_program);
        CHECK(s_last_uniform_loc == loc_for(s_current_program, "u_mode"));
        rnd_uniform(RND_U_USE_TABLE, &mode);
        CHECK(s_last_uniform_program == (GLint)s_current_program);
        CHECK(s_last_uniform_loc == loc_for(s_current_program, "u_use_table"));
    }

    /* A non-table uniform under DYN uses the bound program's cache. */
    rnd_use(RND_DYN);
    {
        float mode = 0.0f;

        rnd_uniform(RND_U_MODE, &mode);
        CHECK(s_last_uniform_program == (GLint)s_draws[0].program);
        CHECK(s_last_uniform_loc == loc_for(s_draws[0].program, "u_mode"));
        draw_dyn(1);
        rnd_uniform(RND_U_MODE, &mode);
        CHECK(s_last_uniform_program == (GLint)s_draws[1].program);
        CHECK(s_last_uniform_loc == loc_for(s_draws[1].program, "u_mode"));
    }

    CHECK(s_double_deletes == 0);
    rnd_shutdown();
    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    CHECK(s_programs_created == s_programs_deleted);
    CHECK(s_shaders_created == s_shaders_deleted);
    CHECK(s_double_deletes == 0);
    CHECK(all_resources_deleted_once());

    if (s_failures == started) {
        printf("%-34s ok\n", label);
    } else {
        fprintf(stderr, "case failed: %s\n", label);
    }
}

#if TEST_EXPECT_SPLIT
static void test_lifecycle(void)
{
    static const char *const exact[] = { "GL_EXT_shader_framebuffer_fetch" };
    GLuint first_plain, first_fetch, first_vao, first_buffer;
    int started = s_failures;

    fake_reset();
    extensions_set(exact, 1);
    CHECK(rnd_init() != 0);
    rnd_use(RND_DYN);
    draw_dyn(0);
    draw_dyn(1);
    CHECK(s_draw_count == 2);
    first_plain = s_draws[0].program;
    first_fetch = s_draws[1].program;
    first_vao = s_draws[0].vao;
    first_buffer = s_draws[0].buffer;
    CHECK(is_plain_dyn(fs_source(first_plain)));
    CHECK(is_fetch_dyn(fs_source(first_fetch)));

    rnd_shutdown();
    CHECK(s_programs_created == s_programs_deleted);
    CHECK(s_shaders_created == s_shaders_deleted);
    CHECK(s_double_deletes == 0);
    CHECK(all_resources_deleted_once());

    s_draw_count = 0;
    s_stream_calls = 0;
    CHECK(rnd_init() != 0);
    CHECK(rnd_dyn_framebuffer_fetch() == 1);
    rnd_use(RND_DYN);
    draw_dyn(0);
    draw_dyn(1);
    CHECK(s_draw_count == 2);
    if (s_draw_count == 2) {
        CHECK(s_draws[0].program != first_plain);
        CHECK(s_draws[1].program != first_fetch);
        CHECK(s_draws[0].vao != first_vao);
        CHECK(s_draws[0].buffer != first_buffer);
        CHECK(is_plain_dyn(fs_source(s_draws[0].program)));
        CHECK(is_fetch_dyn(fs_source(s_draws[1].program)));
    }
    rnd_shutdown();
    CHECK(s_programs_created == s_programs_deleted);
    CHECK(s_shaders_created == s_shaders_deleted);
    CHECK(s_double_deletes == 0);
    CHECK(all_resources_deleted_once());

    if (s_failures == started) {
        puts("lifecycle and reinit               ok");
    }
}
#endif

#if TEST_EXPECT_SPLIT
static void dump_sources(void)
{
    static const char *const exact[] = { "GL_EXT_shader_framebuffer_fetch" };
    const char *plain, *fetch, *vs;
    FILE *f;

    fake_reset();
    extensions_set(exact, 1);
    CHECK(rnd_init() != 0);
    rnd_use(RND_DYN);
    draw_dyn(0);
    draw_dyn(1);
    plain = fs_source(s_draws[0].program);
    fetch = fs_source(s_draws[1].program);
    vs = shader_source(program_for(s_draws[0].program)->vs);
    CHECK(plain != NULL && fetch != NULL && vs != NULL);
    if (plain != NULL && fetch != NULL && vs != NULL) {
        f = fopen(".experiment-evidence/generated_dyn_plain.frag", "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs(plain, f);
            fclose(f);
        }
        f = fopen(".experiment-evidence/generated_dyn_fetch.frag", "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs(fetch, f);
            fclose(f);
        }
        f = fopen(".experiment-evidence/generated_dyn.vert", "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs(vs, f);
            fclose(f);
        }
    }
    rnd_shutdown();
}
#endif

#if TEST_EXPECT_SPLIT
int main(void)
{
    static const char *const exact[] = {
        "GL_EXT_shader_framebuffer_fetch", "GL_OES_standard_derivatives"
    };
    static const char *const near[] = {
        "GL_EXT_shader_framebuffer_fetch_non_coherent",
        "XGL_EXT_shader_framebuffer_fetch"
    };
    static const char *const with_null[] = {
        NULL, "GL_EXT_shader_framebuffer_fetch"
    };

    test_lifetime_detectors();
    run_dyn_case("feature off / no extensions", NULL, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0);
    run_dyn_case("near match only", near, 2,
                 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0);
    run_dyn_case("null extension entry", with_null, 2,
                 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    run_dyn_case("split programs", exact, 2,
                 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    run_dyn_case("fetch compile failure", exact, 2,
                 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0);
    run_dyn_case("fetch link failure", exact, 2,
                 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0);
    run_dyn_case("plain compile failure", exact, 2,
                 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0);
    run_dyn_case("plain link failure", exact, 2,
                 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0);
    run_dyn_case("plain vertex failure", exact, 2,
                 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0);
    run_dyn_case("fallback also fails", exact, 2,
                 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    run_dyn_case("vertex compile failure", exact, 2,
                 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    run_dyn_case("baseline failure clears", exact, 2,
                 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0);
    run_dyn_case("plain use-table location absent", exact, 2,
                 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1);
    run_dyn_case("fetch use-table location absent", exact, 2,
                 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1);
    run_dyn_case("both use-table locations absent", exact, 2,
                 0, 0, 0, 0, 0, 0, 0, 3, 1, 1, 1);
    test_lifecycle();
    dump_sources();

    if (s_failures != 0) {
        fprintf(stderr, "test_perf_dyn_programs: %d failures\n", s_failures);
        return 1;
    }
    puts("ok");
    return 0;
}
#else
int main(void)
{
    static const char *const exact[] = {
        "GL_EXT_shader_framebuffer_fetch", "GL_OES_standard_derivatives"
    };

    run_dyn_case("feature off build", exact, 2,
                 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0);
    if (s_failures != 0) {
        fprintf(stderr, "test_perf_dyn_programs: %d failures\n", s_failures);
        return 1;
    }
    puts("ok");
    return 0;
}
#endif
