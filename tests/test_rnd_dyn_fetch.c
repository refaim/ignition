/*
 * DYN framebuffer-fetch initialisation and fallback, through the real
 * src/rnd_gl.c with a recording GLES surface.
 *
 * TEST_EXPECT_FETCH_BUILD is 1 for the candidate library build and 0 for
 * the default build. The candidate is active only after an exact coherent
 * extension match and a successful DYN compile and link.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLES3/gl3.h"
#include "rnd.h"

#ifndef TEST_EXPECT_FETCH_BUILD
#error "define TEST_EXPECT_FETCH_BUILD=0 or 1"
#endif

enum { MAX_SHADERS = 64, MAX_PROGRAMS = 64, SOURCE_CAP = 16384 };
enum { MAX_GL_EVENTS = 256 };
/* rnd_gl's DYN program stride, and room for its largest pass upload. */
enum { DYN_FLOATS = 9, SHADOW_CAP = 1024 * 1024 };

struct shader_rec {
    int used;
    int failed;
    int fetch;
    GLenum type;
    char source[SOURCE_CAP];
};

struct program_rec {
    int used;
    int failed;
    int fetch;
    GLuint vs, fs;
};

static struct shader_rec s_shaders[MAX_SHADERS];
static struct program_rec s_programs[MAX_PROGRAMS];
static int s_shader_count, s_program_count;
static int s_shaders_deleted, s_programs_deleted;
static int s_fail_fetch_compile, s_fail_fetch_link, s_fail_raster;
static int s_fail_vertex_compile, s_fail_normal_dyn;
static int s_extension_queries;
static const char *const *s_extensions;
static int s_extension_count;
static int s_failures;

enum { EV_DATA, EV_SUB, EV_DRAW };

struct gl_event {
    int kind;
    GLuint buffer;
    GLsizeiptr size;
    GLintptr offset;
    int is_null;
    int first;
    const void *data;
    int missing;
    unsigned long long checksum;
    int payload_ok;
};

static struct gl_event s_events[MAX_GL_EVENTS];
static int s_event_count;
static GLuint s_array_buffer;
static GLuint s_next_buffer;

/* What the fake GL actually holds in the array buffer: an orphan clears
 * it, a sub writes its bytes. Draws compare a run's slice with the bytes
 * the sub supplied, so a right-sized upload of NULL or wrong data fails. */
static unsigned char s_shadow[SHADOW_CAP];
static size_t s_shadow_len;
static GLuint s_shadow_buffer;
static int s_corrupt_sub, s_drop_sub;
/* Exactly what the test handed the production pass API for the last call. */
static const float *s_expected;
static int s_expected_verts;

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

static struct program_rec *program_for(GLuint program)
{
    int i = program_index(program);

    return i >= 0 && s_programs[i].used ? &s_programs[i] : NULL;
}

static void fake_reset(void)
{
    memset(s_shaders, 0, sizeof(s_shaders));
    memset(s_programs, 0, sizeof(s_programs));
    s_shader_count = 0;
    s_program_count = 0;
    s_shaders_deleted = 0;
    s_programs_deleted = 0;
    s_fail_fetch_compile = 0;
    s_fail_fetch_link = 0;
    s_fail_raster = 0;
    s_fail_vertex_compile = 0;
    s_fail_normal_dyn = 0;
    s_extension_queries = 0;
    s_extensions = NULL;
    s_extension_count = 0;
    s_event_count = 0;
    s_array_buffer = 0;
    s_next_buffer = 200;
    s_shadow_len = 0;
    s_shadow_buffer = 0;
    s_corrupt_sub = 0;
    s_drop_sub = 0;
    s_expected = NULL;
    s_expected_verts = 0;
}

static unsigned long long payload_sum(const void *p, size_t n)
{
    const unsigned char *b = p;
    unsigned long long h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void shadow_orphan(GLsizeiptr size)
{
    size_t n = size > 0 ? (size_t)size : 0;

    if (n > SHADOW_CAP) {
        n = SHADOW_CAP;
    }
    memset(s_shadow, 0xCD, n);
    s_shadow_len = 0;
    s_shadow_buffer = s_array_buffer;
}

/* The most recent sub for the buffer the draw reads. */
static const struct gl_event *last_sub(GLuint buffer)
{
    int i;

    for (i = s_event_count - 1; i >= 0; i--) {
        if (s_events[i].kind == EV_SUB && s_events[i].buffer == buffer) {
            return &s_events[i];
        }
    }
    return NULL;
}

/* Exact bytes the production pass supplied for [first, first + count). */
static int draw_payload_ok(GLint first, GLsizei count)
{
    const struct gl_event *sub = last_sub(s_array_buffer);
    size_t start, bytes;

    if (sub == NULL || sub->is_null || sub->missing || sub->data == NULL) {
        return 0;
    }
    if (s_expected == NULL || first < 0 || count < 0 ||
        first + count > s_expected_verts) {
        return 0;
    }
    start = (size_t)first * DYN_FLOATS * sizeof(float);
    bytes = (size_t)count * DYN_FLOATS * sizeof(float);
    if ((size_t)sub->offset + start + bytes > (size_t)sub->size) {
        return 0;
    }
    if ((size_t)sub->offset + start + bytes > s_shadow_len ||
        s_shadow_buffer != sub->buffer) {
        return 0;
    }
    return memcmp(s_shadow + (size_t)sub->offset + start,
                  (const unsigned char *)s_expected + start, bytes) == 0;
}

static int source_count(const char *needle)
{
    int i, n = 0;

    for (i = 0; i < s_shader_count; i++) {
        if (s_shaders[i].used && strstr(s_shaders[i].source, needle) != NULL) {
            n++;
        }
    }
    return n;
}

static const char *source_with(const char *needle)
{
    int i;

    for (i = 0; i < s_shader_count; i++) {
        if (s_shaders[i].used && strstr(s_shaders[i].source, needle) != NULL) {
            return s_shaders[i].source;
        }
    }
    return NULL;
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
    s_shaders[s_shader_count].type = type;
    s_shader_count++;
    return shader;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings,
                    const GLint *length)
{
    struct shader_rec *r;
    size_t used = 0;
    int i, index = shader_index(shader);

    if (index < 0 || !s_shaders[index].used) {
        return;
    }
    r = &s_shaders[index];
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
    r->failed = (r->fetch && s_fail_fetch_compile) ||
                (r->type == GL_VERTEX_SHADER && s_fail_vertex_compile) ||
                (s_fail_normal_dyn && !r->fetch &&
                 strstr(r->source, "v_info.z > 2.5") != NULL) ||
                (s_fail_raster && strstr(r->source, "o_index = a / 255.0;") != NULL);
}

void glCompileShader(GLuint shader)
{
    (void)shader;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    int index = shader_index(shader);

    if (pname != GL_COMPILE_STATUS || index < 0) {
        *params = 0;
        return;
    }
    *params = s_shaders[index].failed ? GL_FALSE : GL_TRUE;
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
    int index = shader_index(shader);

    if (index >= 0 && s_shaders[index].used) {
        s_shaders_deleted++;
    }
}

GLuint glCreateProgram(void)
{
    GLuint program;

    if (s_program_count == MAX_PROGRAMS) {
        return 0;
    }
    program = 5000u + (GLuint)s_program_count;
    s_programs[s_program_count].used = 1;
    s_program_count++;
    return program;
}

void glAttachShader(GLuint program, GLuint shader)
{
    struct program_rec *p = program_for(program);
    int i = shader_index(shader);

    if (p == NULL || i < 0) {
        return;
    }
    if (s_shaders[i].fetch) {
        p->fetch = 1;
    }
    if (s_shaders[i].failed) {
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

    if (p != NULL && p->fetch && s_fail_fetch_link) {
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
    struct program_rec *p = program_for(program);

    if (p != NULL) {
        p->used = 0;
        s_programs_deleted++;
    }
}

void glGetIntegerv(GLenum pname, GLint *data)
{
    if (pname == GL_NUM_EXTENSIONS) {
        s_extension_queries++;
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
    (void)program;
    (void)name;
    return 1;
}

void glGenVertexArrays(GLsizei n, GLuint *arrays)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        arrays[i] = 100u + (GLuint)i;
    }
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    GLsizei i;

    for (i = 0; i < n; i++) {
        buffers[i] = s_next_buffer++;
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
VOID1(glBindVertexArray, GLuint)
VOID1(glDepthFunc, GLenum)
VOID1(glDepthMask, GLboolean)
VOID1(glDisable, GLenum)
VOID1(glEnable, GLenum)
VOID1(glEnableVertexAttribArray, GLuint)
VOID1(glUseProgram, GLuint)
VOID1(glClear, GLbitfield)
VOID1(glClearDepthf, GLfloat)
VOID2(glPolygonOffset, GLfloat, GLfloat)
VOID2(glUniform1i, GLint, GLint)
VOID3(glUniform2f, GLint, GLfloat, GLfloat)
VOID3(glTexParameteri, GLenum, GLenum, GLint)

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{ (void)program; (void)index; (void)name; }
void glBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER) {
        s_array_buffer = buffer;
    }
}
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    (void)usage;
    if (s_event_count < MAX_GL_EVENTS) {
        struct gl_event *e = &s_events[s_event_count++];

        e->kind = EV_DATA;
        e->buffer = s_array_buffer;
        e->size = size;
        e->is_null = data == NULL;
    }
    if (target == GL_ARRAY_BUFFER) {
        shadow_orphan(size);
    }
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    (void)target;
    if (s_event_count < MAX_GL_EVENTS) {
        struct gl_event *e = &s_events[s_event_count++];

        e->kind = EV_SUB;
        e->buffer = s_array_buffer;
        e->offset = offset;
        e->size = size;
        e->is_null = data == NULL;
        e->data = data;
        e->missing = data == NULL || s_drop_sub;
        e->checksum = data != NULL && size > 0
                          ? payload_sum(data, (size_t)size)
                          : 0;
    }
    if (target == GL_ARRAY_BUFFER && data != NULL && !s_drop_sub) {
        if ((size_t)offset + (size_t)size > SHADOW_CAP) {
            if (s_event_count > 0) {
                s_events[s_event_count - 1].missing = 1;
            }
            return;
        }
        memcpy(s_shadow + (size_t)offset, data, (size_t)size);
        if (s_corrupt_sub) {
            s_shadow[offset] ^= 0xFF;
        }
        if ((size_t)offset + (size_t)size > s_shadow_len) {
            s_shadow_len = (size_t)offset + (size_t)size;
        }
        s_shadow_buffer = s_array_buffer;
    }
}
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{ (void)red; (void)green; (void)blue; (void)alpha; }
void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height)
{ (void)target; (void)level; (void)xoffset; (void)yoffset; (void)x; (void)y; (void)width; (void)height; }
void glDeleteBuffers(GLsizei n, const GLuint *buffers) { (void)n; (void)buffers; }
void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { (void)n; (void)framebuffers; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) { (void)n; (void)renderbuffers; }
void glDeleteTextures(GLsizei n, const GLuint *textures) { (void)n; (void)textures; }
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { (void)n; (void)arrays; }
void glBlitFramebuffer(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f,
                       GLint g, GLint h, GLbitfield mask, GLenum filter)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)mask; (void)filter; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    (void)mode;
    if (s_event_count < MAX_GL_EVENTS) {
        struct gl_event *e = &s_events[s_event_count++];

        e->kind = EV_DRAW;
        e->first = first;
        e->size = count;
        e->payload_ok = draw_payload_ok(first, count);
    }
}
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
{ (void)location; (void)v0; (void)v1; (void)v2; (void)v3; }
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat *value)
{ (void)location; (void)count; (void)transpose; (void)value; }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer)
{ (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer; }
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{ (void)x; (void)y; (void)width; (void)height; }

/* --------------------------------------------------------------- cases */

static void run_init_case(const char *label, const char *const *extensions, int count,
                          int fail_compile, int fail_link, int fail_raster,
                          int fail_vertex, int fail_normal_dyn, int expect_init,
                          int expect_active)
{
    int started = s_failures;

    fake_reset();
    extensions_set(extensions, count);
    s_fail_fetch_compile = fail_compile;
    s_fail_fetch_link = fail_link;
    s_fail_raster = fail_raster;
    s_fail_vertex_compile = fail_vertex;
    s_fail_normal_dyn = fail_normal_dyn;

    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    CHECK((rnd_init() != 0) == expect_init);
    CHECK(rnd_dyn_framebuffer_fetch() == expect_active);

    if (expect_active) {
        const char *fetch = source_with("GL_EXT_shader_framebuffer_fetch");
        const char *extension;
        const char *define;
        const char *precision;

        CHECK(fetch != NULL);
        if (fetch != NULL) {
            extension = strstr(fetch, "#extension GL_EXT_shader_framebuffer_fetch : require");
            define = strstr(fetch, "#define IGN_FRAMEBUFFER_FETCH 1");
            precision = strstr(fetch, "precision highp float;");
            CHECK(extension != NULL);
            CHECK(define != NULL);
            CHECK(precision != NULL);
            CHECK(extension != NULL && precision != NULL && extension < precision);
            CHECK(define != NULL && precision != NULL && define < precision);
            CHECK(strstr(fetch, "inout vec2 o_index;") != NULL);
            CHECK(strstr(fetch, "#ifdef IGN_FRAMEBUFFER_FETCH") != NULL);
        }
    }

#if TEST_EXPECT_FETCH_BUILD
    if (count > 0 && extensions != NULL && !fail_vertex &&
        extensions[0] != NULL &&
        strcmp(extensions[0], "GL_EXT_shader_framebuffer_fetch") == 0) {
        CHECK(s_extension_queries == 1);
    }
#else
    CHECK(source_count("GL_EXT_shader_framebuffer_fetch") == 0);
    CHECK(s_extension_queries == 0);
#endif

    if ((fail_compile || fail_link) && !fail_normal_dyn) {
        const char *raster = source_with("o_index = a / 255.0;");

        CHECK(raster != NULL);
        if (raster != NULL) {
            CHECK(strstr(raster, "inout vec2 o_index;") == NULL);
        }
    }
    if (!expect_active && !fail_raster && !fail_normal_dyn && !fail_vertex) {
        CHECK(source_count("out vec2 o_index;") >= 1);
    }

    rnd_shutdown();
    CHECK(rnd_dyn_framebuffer_fetch() == 0);
    CHECK(s_program_count == s_programs_deleted);
    CHECK(s_shader_count == s_shaders_deleted);

    if (s_failures != started) {
        fprintf(stderr, "case failed: %s\n", label);
    } else {
        printf("%-34s ok\n", label);
    }
}

static void pass_upload(const float *verts, int count)
{
    s_expected = verts;
    s_expected_verts = count;
    rnd_stream_pass(verts, count);
}

/* rnd_stream_pass against the real rnd_gl.c: one orphan plus one upload
 * for a pass, offsets drawn from it, and capacity grown then retained. */
static void test_stream_pass(void)
{
    enum { STRIDE = 9, MAX_VERTS = 4096 * 6 };
    static float verts[MAX_VERTS * STRIDE];
    static const char *const exact[] = {
        "GL_EXT_shader_framebuffer_fetch", "GL_OES_standard_derivatives"
    };
    int started = s_failures;
    int base;
    GLuint vbo = 0;
    int i;

    fake_reset();
    extensions_set(exact, 2);
    CHECK(rnd_init() != 0);
    rnd_use(RND_DYN);
    for (i = 0; i < MAX_VERTS * STRIDE; i++) {
        verts[i] = (float)(i % 7);
    }
    base = s_event_count;

    /* Initial DYN capacity is 6 vertices (6 * 9 * 4 = 216 bytes). */
    pass_upload(verts, 3);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].kind == EV_DATA);
    CHECK(s_events[base].is_null == 1);
    CHECK(s_events[base].size == 216);
    CHECK(s_events[base + 1].kind == EV_SUB);
    CHECK(s_events[base + 1].offset == 0);
    CHECK(s_events[base + 1].size == 3 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base].buffer == s_events[base + 1].buffer);
    CHECK(s_events[base + 1].is_null == 0);
    CHECK(s_events[base + 1].missing == 0);
    CHECK(s_events[base + 1].checksum ==
          payload_sum(verts, 3 * STRIDE * sizeof(float)));
    vbo = s_events[base].buffer;
    CHECK(vbo != 0);
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_events[base + 2].kind == EV_DRAW);
    CHECK(s_events[base + 2].first == 0);
    CHECK(s_events[base + 2].size == 3);
    CHECK(s_events[base + 2].payload_ok == 1);

    /* Growth: 30 vertices need 1080 bytes; one data + one sub. */
    base = s_event_count;
    pass_upload(verts, 30);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].kind == EV_DATA);
    CHECK(s_events[base].is_null == 1);
    CHECK(s_events[base].size == 30 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base].buffer == vbo);
    CHECK(s_events[base + 1].kind == EV_SUB);
    CHECK(s_events[base + 1].offset == 0);
    CHECK(s_events[base + 1].size == 30 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base + 1].checksum ==
          payload_sum(verts, 30 * STRIDE * sizeof(float)));
    rnd_draw(RND_TRIANGLES, 0, 15);
    rnd_draw(RND_TRIANGLES, 15, 15);
    CHECK(s_events[base + 2].first == 0);
    CHECK(s_events[base + 3].first == 15);
    CHECK(s_events[base + 3].size == 15);
    CHECK(s_events[base + 2].payload_ok == 1);
    CHECK(s_events[base + 3].payload_ok == 1);

    /* Reuse: same capacity is orphaned again, no growth. */
    base = s_event_count;
    pass_upload(verts, 30);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].size == 30 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base].buffer == vbo);
    CHECK(s_events[base + 1].size == 30 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base + 1].checksum ==
          payload_sum(verts, 30 * STRIDE * sizeof(float)));

    /* A smaller pass keeps the capacity. */
    base = s_event_count;
    pass_upload(verts, 3);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].size == 30 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base + 1].size == 3 * STRIDE * (int)sizeof(float));
    CHECK(s_events[base + 1].checksum ==
          payload_sum(verts, 3 * STRIDE * sizeof(float)));

    /* The maximum pass: 4096 six-vertex faces, 884736 bytes. */
    base = s_event_count;
    pass_upload(verts, MAX_VERTS);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].size == MAX_VERTS * STRIDE * (int)sizeof(float));
    CHECK(s_events[base].size == 884736);
    CHECK(s_events[base].buffer == vbo);
    CHECK(s_events[base + 1].size == 884736);
    rnd_draw(RND_TRIANGLES, 6, 9);
    CHECK(s_events[base + 2].first == 6);
    CHECK(s_events[base + 2].size == 9);
    CHECK(s_events[base + 2].payload_ok == 1);

    /* After the maximum the retained capacity still bounds the pass. */
    base = s_event_count;
    pass_upload(verts, 3);
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base].size == 884736);
    CHECK(s_events[base + 1].size == 3 * STRIDE * (int)sizeof(float));

    /* The oracle rejects a corrupted recorded payload, and a dropped one,
     * as a plain mismatch rather than an environment failure. */
    base = s_event_count;
    s_corrupt_sub = 1;
    pass_upload(verts, 3);
    s_corrupt_sub = 0;
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base + 1].kind == EV_SUB);
    CHECK(s_events[base + 1].missing == 0);
    CHECK(s_events[base + 1].size == 3 * STRIDE * (int)sizeof(float));
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_events[base + 2].kind == EV_DRAW);
    CHECK(s_events[base + 2].payload_ok == 0);
    CHECK(draw_payload_ok(0, 3) == 0);

    base = s_event_count;
    s_drop_sub = 1;
    pass_upload(verts, 3);
    s_drop_sub = 0;
    CHECK(s_event_count == base + 2);
    CHECK(s_events[base + 1].kind == EV_SUB);
    /* The production call supplied real bytes; only the recorder lost them. */
    CHECK(s_events[base + 1].is_null == 0);
    CHECK(s_events[base + 1].missing == 1);
    CHECK(s_events[base + 1].size == 3 * STRIDE * (int)sizeof(float));
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_events[base + 2].kind == EV_DRAW);
    CHECK(s_events[base + 2].payload_ok == 0);
    CHECK(draw_payload_ok(0, 3) == 0);

    /* A clean upload after the probes is accepted again. */
    base = s_event_count;
    pass_upload(verts, 3);
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_events[base + 1].missing == 0);
    CHECK(s_events[base + 2].payload_ok == 1);

    rnd_shutdown();
    if (s_failures == started) {
        puts("stream pass orphan/upload/reuse  ok");
    }
}

int main(void)
{
    static const char *const exact[] = {
        "GL_EXT_shader_framebuffer_fetch", "GL_OES_standard_derivatives"
    };
#if TEST_EXPECT_FETCH_BUILD
    static const char *const none_only[] = {
        "GL_OES_texture_float", "GL_EXT_shader_framebuffer_fetch_non_coherent"
    };
    static const char *const wrong[] = {
        "XGL_EXT_shader_framebuffer_fetch", "GL_EXT_shader_framebuffer_fetch_suffix",
        "GL_EXT_shader_framebuffer_fetch_non_coherent"
    };
    static const char *const with_null[] = {
        NULL, "GL_EXT_shader_framebuffer_fetch"
    };
#endif

    run_init_case("feature off / no extensions", NULL, 0, 0, 0, 0, 0, 0, 1, 0);
#if TEST_EXPECT_FETCH_BUILD
    run_init_case("non-coherent only", none_only, 2, 0, 0, 0, 0, 0, 1, 0);
    run_init_case("near-match only", wrong, 3, 0, 0, 0, 0, 0, 1, 0);
    run_init_case("null extension entry", with_null, 2, 0, 0, 0, 0, 0, 1, 1);
    run_init_case("exact coherent", exact, 2, 0, 0, 0, 0, 0, 1, 1);
    run_init_case("fetch compile failure", exact, 2, 1, 0, 0, 0, 0, 1, 0);
    run_init_case("fetch link failure", exact, 2, 0, 1, 0, 0, 0, 1, 0);
    run_init_case("fallback also fails", exact, 2, 1, 0, 0, 0, 1, 0, 0);
    run_init_case("vertex compile failure", exact, 2, 0, 0, 0, 1, 0, 0, 0);
    run_init_case("baseline failure clears", exact, 2, 0, 0, 1, 0, 0, 0, 0);
#else
    run_init_case("coherent, default build off", exact, 2, 0, 0, 0, 0, 0, 1, 0);
#endif
    test_stream_pass();

    if (s_failures != 0) {
        fprintf(stderr, "test_rnd_dyn_fetch: %d failures\n", s_failures);
        return 1;
    }
    puts("ok");
    return 0;
}
