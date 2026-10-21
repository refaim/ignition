/*
 * gfx_gl.c - OpenGL 3.2 core renderer.
 */

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include <stdio.h>

#include "gfx.h"

static GLuint s_program;
static GLuint s_vao;
static GLuint s_index_tex;      /* R8, the surface */
static GLuint s_palette_tex;    /* 256 x 1 RGB8 */
static GLint s_u_rect;
static GLuint s_over_program;   /* the screen over a 3D layer */
static GLint s_over_u_rect;
static GLuint s_snap_tex;       /* R8, the screen as the 3D left it */
static int s_tex_w, s_tex_h;
static int s_draw_w, s_draw_h;

/* A quad from gl_VertexID; u_rect is x0, y0, x1, y1 in clip space. */
static const char *s_vs =
    "#version 150\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    v_uv = vec2(c.x, 1.0 - c.y);\n"
    "    gl_Position = vec4(mix(u_rect.xy, u_rect.zw, c), 0.0, 1.0);\n"
    "}\n";

static const char *s_fs =
    "#version 150\n"
    "uniform sampler2D u_index;\n"
    "uniform sampler2D u_palette;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float i = texture(u_index, v_uv).r * 255.0;\n"
    "    o_color = vec4(texture(u_palette, vec2((i + 0.5) / 256.0, 0.5)).rgb, 1.0);\n"
    "}\n";

/* Shows the screen's pixels where they differ from the snapshot taken
 * after the 3D was drawn into it: what the 2D drawing changed. */
static const char *s_over_fs =
    "#version 150\n"
    "uniform sampler2D u_index;\n"
    "uniform sampler2D u_palette;\n"
    "uniform sampler2D u_snap;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    float i = texture(u_index, v_uv).r * 255.0;\n"
    "    float s = texture(u_snap, v_uv).r * 255.0;\n"
    "    if (abs(i - s) < 0.5) discard;\n"
    "    o_color = vec4(texture(u_palette, vec2((i + 0.5) / 256.0, 0.5)).rgb, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "gfx: shader: %s\n", log);
    }
    return s;
}

static GLuint make_texture(void)
{
    GLuint t;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

int gfx_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, s_vs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fs);
    GLint ok;

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "gfx: shader link failed\n");
        return 0;
    }
    glUseProgram(s_program);
    glUniform1i(glGetUniformLocation(s_program, "u_index"), 0);
    glUniform1i(glGetUniformLocation(s_program, "u_palette"), 1);
    s_u_rect = glGetUniformLocation(s_program, "u_rect");

    glGenVertexArrays(1, &s_vao);
    s_index_tex = make_texture();
    s_palette_tex = make_texture();
    glBindTexture(GL_TEXTURE_2D, s_palette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    s_snap_tex = make_texture();

    vs = compile(GL_VERTEX_SHADER, s_vs);
    fs = compile(GL_FRAGMENT_SHADER, s_over_fs);
    s_over_program = glCreateProgram();
    glAttachShader(s_over_program, vs);
    glAttachShader(s_over_program, fs);
    glBindFragDataLocation(s_over_program, 0, "o_color");
    glLinkProgram(s_over_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(s_over_program);
    glUniform1i(glGetUniformLocation(s_over_program, "u_index"), 0);
    glUniform1i(glGetUniformLocation(s_over_program, "u_palette"), 1);
    glUniform1i(glGetUniformLocation(s_over_program, "u_snap"), 2);
    s_over_u_rect = glGetUniformLocation(s_over_program, "u_rect");
    return 1;
}

void gfx_shutdown(void)
{
    glDeleteTextures(1, &s_index_tex);
    glDeleteTextures(1, &s_palette_tex);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_program);
}

void gfx_begin_frame(int width, int height)
{
    s_draw_w = width;
    s_draw_h = height;
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_draw_screen(const u8 *pixels, int w, int h, int pitch,
                     const u8 *palette, float aspect)
{
    float sw, sh;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_index_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    if (w != s_tex_w || h != s_tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
        s_tex_w = w;
        s_tex_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette);

    /* Fill the height; pillarbox, or letterbox when the window is
     * narrower than the screen. */
    sh = 1.0f;
    sw = aspect * (float)s_draw_h / (float)s_draw_w;
    if (sw > 1.0f) {
        sh = 1.0f / sw;
        sw = 1.0f;
    }
    glUseProgram(s_program);
    glUniform4f(s_u_rect, -sw, -sh, sw, sh);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* The rectangle a screen of `aspect` takes: the drawable's height,
 * pillarboxed, or letterboxed when the window is narrower. */
static void screen_rect(float aspect, float *sw, float *sh)
{
    *sh = 1.0f;
    *sw = aspect * (float)s_draw_h / (float)s_draw_w;
    if (*sw > 1.0f) {
        *sh = 1.0f / *sw;
        *sw = 1.0f;
    }
}

static void upload(GLuint tex, int *tw, int *th, const u8 *pixels, int w, int h, int pitch)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    if (w != *tw || h != *th) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
        *tw = w;
        *th = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void gfx_draw_layers(unsigned layer, const u8 *pixels, const u8 *snapshot, int w, int h,
                     int pitch, const u8 *palette, float aspect)
{
    static int snap_w, snap_h;
    float sw, sh;

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(s_vao);

    /* The 3D, the whole drawable. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)layer);
    glUseProgram(s_program);
    glUniform4f(s_u_rect, -1.0f, -1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* What the 2D drawing changed, at the screen's place. */
    glActiveTexture(GL_TEXTURE0);
    upload(s_index_tex, &s_tex_w, &s_tex_h, pixels, w, h, pitch);
    glActiveTexture(GL_TEXTURE2);
    upload(s_snap_tex, &snap_w, &snap_h, snapshot, w, h, pitch);
    screen_rect(aspect, &sw, &sh);
    glUseProgram(s_over_program);
    glUniform4f(s_over_u_rect, -sw, -sh, sw, sh);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glActiveTexture(GL_TEXTURE0);
}

void gfx_read_rgb(int w, int h, u8 *rgb)
{
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}

void gfx_draw_index_texture(unsigned tex, const u8 *palette, float aspect)
{
    float sw, sh;

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, palette);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    screen_rect(aspect, &sw, &sh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(s_program);
    glUniform4f(s_u_rect, -sw, -sh, sw, sh);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
