/*
 * test_rnd_counts.c - what the real renderer call sites report to
 * prof_count, observed through the actual engine library.
 *
 * The library must be built with IGN_PROF (libignition.so). This
 * executable defines prof_count, which interposes over the library's
 * definition, so link it with -Wl,--export-dynamic. The rnd_* functions
 * themselves are the real ones, unchanged.
 *
 * A 64x64 EGL pbuffer with an ES3 context is enough: no assets are
 * needed. Draws use count 0, which still executes each call site and its
 * primitive/strip branch without requiring a full shader setup. Rendering
 * correctness is covered by the separate GPU fixture.
 *
 * Exit 0 passes, 1 is a counter failure, 2 is an EGL/rnd_init failure.
 */

#ifndef IGN_PROF
#define IGN_PROF 1
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "rnd.h"

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT EGL_OPENGL_ES3_BIT_KHR
#endif

static uint64_t s_counts[PROF_COUNTERS];
static int s_failures;

/* The library's call sites bind to this definition. */
__attribute__((visibility("default")))
void prof_count(int counter, uint64_t amount)
{
    s_counts[counter] += amount;
}

static void counts_reset(void)
{
    memset(s_counts, 0, sizeof(s_counts));
}

static void check_count(const char *what, int counter, uint64_t want)
{
    if (s_counts[counter] != want) {
        fprintf(stderr, "FAIL: %s is %llu, expected %llu\n", what,
                (unsigned long long)s_counts[counter], (unsigned long long)want);
        s_failures++;
    }
}

static void check_no_gl_error(const char *where)
{
    GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: GL error 0x%04x after %s\n", (unsigned)error, where);
        s_failures++;
    }
}

static int egl_begin(EGLDisplay *display, EGLSurface *surface, EGLContext *context)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint surface_attrs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };
    static const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    EGLConfig config;
    EGLint count = 0;

    *display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (*display == EGL_NO_DISPLAY || !eglInitialize(*display, NULL, NULL)) {
        fprintf(stderr, "FAIL: no EGL display\n");
        return 0;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(*display, config_attrs, &config, 1, &count) || count != 1) {
        fprintf(stderr, "FAIL: no ES3 EGL config\n");
        return 0;
    }
    *surface = eglCreatePbufferSurface(*display, config, surface_attrs);
    if (*surface == EGL_NO_SURFACE) {
        fprintf(stderr, "FAIL: no EGL pbuffer\n");
        return 0;
    }
    *context = eglCreateContext(*display, config, EGL_NO_CONTEXT, context_attrs);
    if (*context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(*display, *surface, *surface, *context)) {
        fprintf(stderr, "FAIL: no ES3 EGL context\n");
        return 0;
    }
    return eglGetCurrentContext() != EGL_NO_CONTEXT;
}

int main(void)
{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    float stream4[4 * 4];
    float stream5[5 * 4];
    float mesh[3 * 8];
    unsigned color, copy, sample, target;

    memset(stream4, 0, sizeof(stream4));
    memset(stream5, 0, sizeof(stream5));
    memset(mesh, 0, sizeof(mesh));

    if (!egl_begin(&display, &surface, &context)) {
        return 2;
    }
    if (!rnd_init()) {
        fprintf(stderr, "FAIL: rnd_init() failed\n");
        return 2;
    }
    check_no_gl_error("rnd_init");

    color = rnd_texture(RND_RG8, 64, 64, NULL, 0);
    copy = rnd_texture(RND_RG8, 64, 64, NULL, 0);
    sample = rnd_texture(RND_R8, 64, 64, NULL, 0);
    target = rnd_target(color, 0);
    if (color == 0 || copy == 0 || sample == 0 || target == 0) {
        fprintf(stderr, "FAIL: texture or target creation failed\n");
        return 2;
    }
    check_no_gl_error("texture creation");

    /* Texture uploads and NULL buffer allocations are not counted. */
    check_count("copy calls", PROF_COPY_CALLS, 0);
    check_count("copy pixels", PROF_COPY_PIXELS, 0);
    check_count("draw calls", PROF_DRAW_CALLS, 0);
    check_count("upload bytes", PROF_UPLOAD_BYTES, 0);

    /* Two requested rectangles: calls and requested pixels. */
    counts_reset();
    rnd_bind(target);
    rnd_copy(copy, 0, 0, 4, 5);
    rnd_copy(copy, 8, 4, 3, 2);
    check_count("copy calls", PROF_COPY_CALLS, 2);
    check_count("copy pixels", PROF_COPY_PIXELS, 4 * 5 + 3 * 2);
    check_no_gl_error("rnd_copy");

    /* CANVAS has stride 4 and an initial 4-vertex buffer, so the second
     * stream grows it. Both submissions are counted. */
    counts_reset();
    rnd_use(RND_CANVAS);
    rnd_stream(stream4, 4);
    rnd_stream(stream5, 5);
    check_count("stream bytes", PROF_UPLOAD_BYTES, (4 + 5) * 4 * sizeof(float));
    check_no_gl_error("rnd_stream");

    /* Allocating the mesh store is not a submission; writing 3 of the 4
     * vertices is. first does not change the counted count. */
    counts_reset();
    rnd_mesh_store(4);
    check_count("mesh store bytes", PROF_UPLOAD_BYTES, 0);
    rnd_mesh_write(1, 3, mesh);
    check_count("mesh write bytes", PROF_UPLOAD_BYTES, 3 * 8 * sizeof(float));
    check_no_gl_error("rnd_mesh_store/rnd_mesh_write");

    /* One call site each: triangles, strip and mesh. Count 0 keeps the
     * draw valid without a full texture setup; the strip branch still
     * executes. */
    counts_reset();
    rnd_use(RND_CANVAS);
    rnd_texture_unit(0, sample);
    rnd_texture_unit(1, sample);
    rnd_texture_unit(2, sample);
    rnd_texture_unit(3, sample);
    rnd_draw(RND_TRIANGLES, 0, 0);
    rnd_draw(RND_STRIP, 0, 0);
    rnd_texture_unit(0, 0);
    rnd_use(RND_MESH);
    rnd_draw_mesh(0, 0);
    check_count("draw calls", PROF_DRAW_CALLS, 3);
    check_count("draw upload bytes", PROF_UPLOAD_BYTES, 0);
    check_no_gl_error("rnd_draw/rnd_draw_mesh");

    rnd_target_free(target);
    rnd_texture_free(color);
    rnd_texture_free(copy);
    rnd_texture_free(sample);
    rnd_shutdown();
    check_no_gl_error("shutdown");

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (s_failures != 0) {
        puts("FAIL");
        return 1;
    }
    puts("ok");
    return 0;
}
