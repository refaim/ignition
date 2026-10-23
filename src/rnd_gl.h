/*
 * rnd_gl.h - what the hosts tell rnd_gl.c.
 *
 * rnd_gl.c runs on three kinds of OpenGL:
 *
 *   OpenGL 3.2 core, from the system's headers   macOS
 *   OpenGL 3.2 core, through rnd_gl_load         Linux, Windows
 *   OpenGL ES 3.0 (IGN_GLES)                     the web, Android, iOS
 */

#ifndef IGN_RND_GL_H
#define IGN_RND_GL_H

/* Linux and Windows: looks up every OpenGL entry point rnd_gl.c calls
 * with `get` (glXGetProcAddressARB, wglGetProcAddress), once the
 * context is current. Returns 0, naming the first one missing, when one
 * is missing. Elsewhere it does nothing and returns 1. */
int rnd_gl_load(void *(*get)(const char *name));

/* The window's framebuffer: 0, except on iOS, where the window is a
 * framebuffer object the host makes. rnd_bind_screen binds it. */
extern unsigned rnd_gl_screen_fbo;

#endif /* IGN_RND_GL_H */
