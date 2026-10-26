/*
 * rnd_discard_depth through the real src/rnd_gl.c with a recording GLES
 * surface: only the currently bound nonzero target is invalidated, the
 * attachment list is exactly GL_DEPTH_ATTACHMENT, and no colour is named.
 */

#include <stdio.h>

#define IGN_GLES 1
#define __ANDROID__ 1

#include "GLES3/gl3.h"
#include "rnd.h"

#include "../src/rnd_gl.c"

static int s_calls;
static GLenum s_target;
static GLsizei s_count;
static GLenum s_attachment;
static int s_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

void glInvalidateFramebuffer(GLenum target, GLsizei num_attachments,
                             const GLenum *attachments)
{
    s_calls++;
    s_target = target;
    s_count = num_attachments;
    s_attachment = num_attachments > 0 ? attachments[0] : 0;
}

int main(void)
{
    /* Target 0 (the screen) is never invalidated. */
    rnd_discard_depth(0);
    CHECK(s_calls == 0);

    /* A target that is not the bound one is not invalidated. */
    rnd_discard_depth(5);
    CHECK(s_calls == 0);

    /* The bound target's depth attachment, and only that, is dropped. */
    s_bound = 5;
    rnd_discard_depth(5);
    CHECK(s_calls == 1);
    CHECK(s_target == GL_FRAMEBUFFER);
    CHECK(s_count == 1);
    CHECK(s_attachment == GL_DEPTH_ATTACHMENT);
    CHECK(s_attachment != GL_COLOR_ATTACHMENT0);

    /* Another target while 5 stays bound is not invalidated. */
    rnd_discard_depth(7);
    CHECK(s_calls == 1);

    if (s_failures != 0) {
        fprintf(stderr, "test_rnd_discard: %d failures; calls=%d\n", s_failures, s_calls);
        puts("FAIL");
        return 1;
    }
    puts("ok");
    return 0;
}
