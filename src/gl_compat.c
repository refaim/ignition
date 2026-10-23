/*
 * gl_compat.c - what gl_compat.h needs on the PlayStation Vita.
 *
 * The renderer reads the pixels under a translucent face from a copy of
 * the framebuffer. vitaGL's glCopyTexSubImage2D reads back to the CPU
 * and uploads again, stalling the GPU each time; here the copy is a blit
 * into a framebuffer made for the texture, which stays on the GPU.
 */

#include "gl_compat.h"

#ifdef IGN_GL_VITA

enum { COPY_TARGETS = 32 };

static struct {
    GLuint tex, fbo;
} s_copy[COPY_TARGETS];
static int s_next;

static GLuint copy_fbo(GLuint tex)
{
    int i;

    for (i = 0; i < COPY_TARGETS; i++) {
        if (s_copy[i].tex == tex && s_copy[i].fbo != 0) {
            return s_copy[i].fbo;
        }
    }
    i = s_next;
    s_next = (s_next + 1) % COPY_TARGETS;
    if (s_copy[i].fbo == 0) {
        glGenFramebuffers(1, &s_copy[i].fbo);
    }
    s_copy[i].tex = tex;
    glBindFramebuffer(GL_FRAMEBUFFER, s_copy[i].fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return s_copy[i].fbo;
}

void ign_copy_rect(GLuint fbo, GLuint tex, int x, int y, int w, int h)
{
    GLuint dst = copy_fbo(tex);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
    glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void ign_forget_texture(GLuint tex)
{
    int i;

    for (i = 0; i < COPY_TARGETS; i++) {
        if (s_copy[i].tex == tex) {
            s_copy[i].tex = 0;
        }
    }
}

#endif
