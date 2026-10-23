/*
 * gl_compat.h - the OpenGL header for each host, and the few places the
 * hosts' OpenGL differ.
 *
 * macOS (and other desktops): OpenGL 3.2 core, shaders in GLSL 150.
 *
 * PlayStation Vita: vitaGL over GXM. The shaders are given in Cg
 * (vitaGL compiles them on the device with the system's shader
 * compiler), so each file carries a Cg copy of its shaders under
 * IGN_GL_VITA. vitaGL has no texture arrays: gl3d.c lays a texture
 * slot's pages out as tiles of one 2D texture. Copying the framebuffer
 * into a texture goes through a blit (gl_compat.c), as vitaGL's
 * glCopyTexSubImage2D reads the pixels back to the CPU.
 *
 * Must be included instead of any GL header.
 */

#ifndef IGN_GL_COMPAT_H
#define IGN_GL_COMPAT_H

#if defined(__vita__)

#include <vitaGL.h>

#define IGN_GL_VITA 1
#define IGN_VERTEX_SHADER GL_CG_VERTEX_SHADER_EXT
#define IGN_FRAGMENT_SHADER GL_CG_FRAGMENT_SHADER_EXT

/* Cg names its output by semantic (COLOR). */
#define glBindFragDataLocation(program, color, name) \
    ((void)(program), (void)(color), (void)(name))

/* A palette index as a colour channel. GXM packs the shader's float
 * output to 8 bits; a quarter step up keeps the index whether it rounds
 * or truncates. */
#define IGN_INDEX_F(i) (((float)(i) + 0.25f) / 255.0f)

/* Copies the rectangle (x, y, w, h) of framebuffer `fbo` into the same
 * place of texture `tex`. */
void ign_copy_rect(GLuint fbo, GLuint tex, int x, int y, int w, int h);

/* `tex` is about to be deleted: drops what ign_copy_rect keeps for it. */
void ign_forget_texture(GLuint tex);

#else

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#define IGN_VERTEX_SHADER GL_VERTEX_SHADER
#define IGN_FRAGMENT_SHADER GL_FRAGMENT_SHADER

#define IGN_INDEX_F(i) ((float)(i) / 255.0f)

/* The source is the bound framebuffer; the texture is bound to the
 * active unit. */
static inline void ign_copy_rect(GLuint fbo, GLuint tex, int x, int y, int w, int h)
{
    (void)fbo;
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x, y, x, y, w, h);
}

static inline void ign_forget_texture(GLuint tex)
{
    (void)tex;
}

#endif

#endif /* IGN_GL_COMPAT_H */
