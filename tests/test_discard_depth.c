/*
 * The final 3D view's depth attachment is discarded before the view's
 * framebuffer is unbound for the 2D drawing. The test drives the real
 * race_draw_view finalization and gl3d view lifetime with a recording
 * rnd backend, so the ordering and target are observed at the production
 * calls rather than in a test-only helper. It covers a single view, the
 * two split-screen halves (the first half must keep its depth for the
 * resume), the HUD's 2D binding between them, a screen back to one view,
 * repeated frames, an empty drawable, and a resized/recreated view.
 * EXPECT_DISCARDS=0 builds it against the feature-off productions.
 */

#include <stdio.h>
#include <string.h>

#ifdef IGN_PROF
#include <stdint.h>

void prof_begin(int part)
{
    (void)part;
}

void prof_end(int part)
{
    (void)part;
}

void prof_frame(void)
{
}

void prof_count(int counter, uint64_t amount)
{
    (void)counter;
    (void)amount;
}
#endif

#include "gl3d.h"
#include "race/race.h"
#include "rnd.h"
#include "startup.h"

enum {
    MAX_EVENTS = 512,
    MAX_TEXTURES = 128,
    MAX_TARGETS = 64,
    MAX_FREED = 128
};

enum event_kind {
    EVENT_BIND,
    EVENT_DRAW,
    EVENT_DISCARD
};

struct event {
    enum event_kind kind;
    unsigned target;
    unsigned bound;
    int first;
    int count;
    int depth_attachment;
    int attachment_count;
    int attachment;
};

struct texture_rec {
    unsigned id;
    int format;
    int w;
    int h;
};

struct target_rec {
    unsigned id;
    unsigned color;
    int depth_attachment;
};

static struct event s_events[MAX_EVENTS];
static int s_event_count;
static struct texture_rec s_textures[MAX_TEXTURES];
static int s_texture_count;
static struct target_rec s_targets[MAX_TARGETS];
static int s_target_count;
static unsigned s_bound;
static unsigned s_mesh_vbo;
static int s_mesh_vbo_size;
static int s_mesh_vbo_used;
static int s_shader;
static int s_failures;
static int s_layer_calls;
static unsigned s_layer_texture;
static int s_total_textures;
static int s_total_targets;
static unsigned s_freed[MAX_FREED];
static int s_freed_count;
static int s_drawable_w = 320;
static int s_drawable_h = 200;
static int s_internal_w = 320;
static int s_internal_h = 200;

/* The flag-off and non-Android builds expect no discard. */
#ifndef EXPECT_DISCARDS
#define EXPECT_DISCARDS 1
#endif

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

static struct event *event_add(enum event_kind kind)
{
    struct event *e;

    if (s_event_count >= MAX_EVENTS) {
        fprintf(stderr, "FAIL: event recorder full\n");
        s_failures++;
        return &s_events[MAX_EVENTS - 1];
    }
    e = &s_events[s_event_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->bound = s_bound;
    return e;
}

static int texture_index(unsigned tex)
{
    int i;

    for (i = 0; i < s_texture_count; i++) {
        if (s_textures[i].id == tex) {
            return i;
        }
    }
    return -1;
}

static int target_index(unsigned target)
{
    int i;

    for (i = 0; i < s_target_count; i++) {
        if (s_targets[i].id == target) {
            return i;
        }
    }
    return -1;
}

unsigned rnd_texture(int format, int w, int h, const void *pixels, int pitch)
{
    struct texture_rec *r;

    CHECK(s_texture_count < MAX_TEXTURES);
    CHECK(w > 0 && h > 0);
    CHECK(pixels == NULL || pitch == 0 || pitch >= w);
    if (s_texture_count >= MAX_TEXTURES) {
        return 0;
    }
    r = &s_textures[s_texture_count];
    r->id = 500u + (unsigned)s_texture_count;
    r->format = format;
    r->w = w;
    r->h = h;
    s_texture_count++;
    s_total_textures++;
    return r->id;
}

void rnd_texture_set(unsigned tex, int w, int h, const void *pixels, int pitch)
{
    int i = texture_index(tex);

    CHECK(i >= 0);
    CHECK(w > 0 && h > 0);
    CHECK(pixels == NULL || pitch == 0 || pitch >= w);
    if (i >= 0) {
        s_textures[i].w = w;
        s_textures[i].h = h;
    }
}

void rnd_texture_free(unsigned tex)
{
    CHECK(s_freed_count < MAX_FREED);
    if (s_freed_count < MAX_FREED) {
        s_freed[s_freed_count++] = tex;
    }
}

unsigned rnd_pages(const u8 *pixels, int count)
{
    CHECK(pixels == NULL || count > 0);
    return 900u;
}

void rnd_copy(unsigned tex, int x, int y, int w, int h)
{
    (void)tex;
    CHECK(w > 0 && h > 0);
    CHECK(x >= 0 && y >= 0);
}

unsigned rnd_target(unsigned color, int depth)
{
    struct target_rec *r;

    CHECK(s_target_count < MAX_TARGETS);
    CHECK(texture_index(color) >= 0);
    if (s_target_count >= MAX_TARGETS) {
        return 0;
    }
    r = &s_targets[s_target_count];
    r->id = 700u + (unsigned)s_target_count;
    r->color = color;
    r->depth_attachment = depth ? 1 : 0;
    s_target_count++;
    s_total_targets++;
    return r->id;
}

void rnd_target_free(unsigned target)
{
    (void)target;
}

void rnd_bind(unsigned target)
{
    struct event *e;

    CHECK(target != 0);
    CHECK(target_index(target) >= 0);
    s_bound = target;
    e = event_add(EVENT_BIND);
    e->target = target;
}

void rnd_bind_screen(int w, int h)
{
    CHECK(w > 0 && h > 0);
    s_bound = 0;
}

void rnd_clear(const float *rgba, int depth)
{
    CHECK(rgba != NULL || depth);
}

void rnd_read(int x, int y, int w, int h, int format, void *out, int pitch)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)format;
    (void)out;
    (void)pitch;
}

void rnd_blit(unsigned src, unsigned dst, int sw, int sh, int dw, int dh)
{
    CHECK(target_index(src) >= 0);
    CHECK(target_index(dst) >= 0);
    CHECK(sw > 0 && sh > 0 && dw > 0 && dh > 0);
}

void rnd_scissor(int on, int x, int y, int w, int h)
{
    (void)on;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void rnd_depth(int func, int write)
{
    (void)func;
    (void)write;
}

void rnd_cull(int mode)
{
    (void)mode;
}

void rnd_offset(int on)
{
    (void)on;
}

void rnd_use(int program)
{
    CHECK(program >= 0 && program < RND_PROGRAMS);
    s_shader = program;
}

void rnd_uniform(int id, const float *v)
{
    CHECK(id >= 0 && id < RND_UNIFORMS);
    CHECK(v != NULL);
}

void rnd_texture_unit(int unit, unsigned tex)
{
    CHECK(unit >= 0 && unit < 4);
    CHECK(tex == 0 || texture_index(tex) >= 0 || tex == 900u);
}

void rnd_stream(const float *verts, int count)
{
    CHECK(verts != NULL || count == 0);
    CHECK(count >= 0);
}

void rnd_draw(int prim, int first, int count)
{
    struct event *e;

    (void)prim;
    e = event_add(EVENT_DRAW);
    e->target = s_bound;
    e->first = first;
    e->count = count;
}

void rnd_mesh_store(int count)
{
    CHECK(count > 0);
    s_mesh_vbo = 1300u;
    s_mesh_vbo_size = count;
    s_mesh_vbo_used = 0;
}

void rnd_mesh_write(int first, int count, const float *verts)
{
    CHECK(verts != NULL || count == 0);
    CHECK(first >= 0 && count >= 0);
    CHECK(first + count <= s_mesh_vbo_size);
    if (first + count > s_mesh_vbo_used) {
        s_mesh_vbo_used = first + count;
    }
}

void rnd_draw_mesh(int first, int count)
{
    struct event *e;

    CHECK(s_mesh_vbo != 0);
    CHECK(first >= 0 && count > 0);
    CHECK(first + count <= s_mesh_vbo_used);
    e = event_add(EVENT_DRAW);
    e->target = s_bound;
    e->first = first;
    e->count = count;
}

void rnd_present(void)
{
}

void rnd_texture_size(unsigned tex, int *w, int *h)
{
    int i = texture_index(tex);

    CHECK(i >= 0);
    if (i >= 0) {
        *w = s_textures[i].w;
        *h = s_textures[i].h;
    }
}

#if defined(IGN_GLES)
int rnd_dyn_framebuffer_fetch(void)
{
    return 0;
}
#endif

int rnd_batch_copies(void)
{
    return 0;
}

void rnd_discard_depth(unsigned target)
{
    struct event *e;
    int i = target_index(target);

    e = event_add(EVENT_DISCARD);
    e->target = target;
    e->depth_attachment = i >= 0 && s_targets[i].depth_attachment;
    e->attachment_count = 1;
    e->attachment = 0x8d00;
}

void scene_view_rows(const struct scene *sc, double rows[3][3])
{
    memset(rows, 0, 9 * sizeof(double));
    rows[0][0] = rows[1][1] = rows[2][2] = 1.0;
    (void)sc;
}

void scene_obj_rows(const struct scene_obj *o, double rows[3][3])
{
    memset(rows, 0, 9 * sizeof(double));
    rows[0][0] = rows[1][1] = rows[2][2] = 1.0;
    (void)o;
}

void scene_set_reach(double reach)
{
    CHECK(reach > 0.0);
}

int scene_gather_view(void)
{
    return g_scene->obj_count;
}

void video_drawable_size(int *w, int *h)
{
    *w = s_drawable_w;
    *h = s_drawable_h;
}

void canvas_internal_size(int *w43, int *h)
{
    *w43 = s_internal_w;
    *h = s_internal_h;
}

void video_set_layer(unsigned layer, const u8 *snapshot)
{
    (void)snapshot;
    CHECK(layer != 0);
    CHECK(texture_index(layer) >= 0);
    s_layer_calls++;
    s_layer_texture = layer;
}

void soft3d_race_view(int first)
{
    (void)first;
}

void soft3d_raster(u8 *target, int w, int h, int pitch, const struct raster_cmd *cmds, int count,
                   const u8 *pages)
{
    (void)target;
    (void)w;
    (void)h;
    (void)pitch;
    (void)cmds;
    (void)count;
    (void)pages;
}

int canvas_is_gpu(const u8 *key)
{
    (void)key;
    return 0;
}

void canvas_extent(const u8 *key, float *mx, float *my)
{
    (void)key;
    *mx = 0.0f;
    *my = 0.0f;
}

unsigned canvas_bind(u8 *key, int *pw, int *ph, int clear_depth)
{
    (void)key;
    (void)clear_depth;
    *pw = 1;
    *ph = 1;
    return 0;
}

unsigned canvas_snapshot(const u8 *key)
{
    (void)key;
    return 0;
}

unsigned canvas_snapshot_rect(const u8 *key, float x0, float y0, float x1, float y1)
{
    (void)key;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    return 0;
}

int g_soft3d;
s32 g_screen_width = 320;
s32 g_screen_height = 200;
u8 g_back_buffer[SCREEN_MAX_W * SCREEN_MAX_H];
float g_view_scale = 1.0f;
struct scene *g_scene;
struct scene_obj **g_scene_visible;
u8 *g_scene_textures[SCENE_TEXTURES];
int g_scene_texture_pages[SCENE_TEXTURES];
s32 g_scene_group;
s32 g_two_players;
s32 r_552f58;

static struct scene s_scene;
static struct scene_obj *s_visible[1];

static void reset_records(void)
{
    s_event_count = 0;
    s_bound = 0;
    s_shader = -1;
    s_layer_calls = 0;
    s_layer_texture = 0;
}

static int event_index(enum event_kind kind, unsigned target, int from)
{
    int i;

    for (i = from; i < s_event_count; i++) {
        if (s_events[i].kind == kind && s_events[i].target == target) {
            return i;
        }
    }
    return -1;
}

static int texture_freed(unsigned tex)
{
    int i;

    for (i = 0; i < s_freed_count; i++) {
        if (s_freed[i] == tex) {
            return 1;
        }
    }
    return 0;
}

/* The frame's one final-view discard, or none when the feature is off:
 * after the last draw, on the view's own nonzero depth target, bound at
 * the call, and the last GL event before the 2D binding takes over. The
 * attachment list names only the depth, so the colour stays readable. */
static void check_frame(int want_discards, const char *what)
{
    int draws = 0, discards = 0, last_draw = -1, discard_at = -1;
    int view_bind = -1;
    unsigned view_target = 0;
    int i;

    for (i = 0; i < s_event_count; i++) {
        const struct event *e = &s_events[i];

        if (e->kind == EVENT_DRAW) {
            draws++;
            last_draw = i;
        } else if (e->kind == EVENT_DISCARD) {
            discards++;
            discard_at = i;
            CHECK(e->target != 0);
            CHECK(e->attachment_count == 1);
            CHECK(e->attachment == 0x8d00);
        } else if (e->kind == EVENT_BIND && view_bind < 0) {
            view_bind = i;
            view_target = e->target;
        }
    }

    CHECK(draws > 0);
    CHECK(view_bind >= 0);
    if (discards != want_discards) {
        fprintf(stderr, "FAIL %s: %d discards, expected %d\n", what, discards,
                want_discards);
        s_failures++;
    }
    if (want_discards != 0) {
        CHECK(EXPECT_DISCARDS);
        CHECK(discard_at > last_draw);
        CHECK(discard_at == s_event_count - 1);
        CHECK(s_events[discard_at].bound == view_target);
        CHECK(s_events[discard_at].target == view_target);
        CHECK(s_events[discard_at].depth_attachment);
        CHECK(target_index(view_target) >= 0 &&
              s_targets[target_index(view_target)].depth_attachment);
    }
    CHECK(s_layer_calls > 0);
    CHECK(texture_index(s_layer_texture) >= 0);
    CHECK(!texture_freed(s_layer_texture));
}

static void prepare_scene(void)
{
    static const s32 triangle[] = {
        3, 1,
        -10, 0, 10,
        10, 0, 10,
        0, 10, 10,
        15, 0, 1, 2, 251
    };
    static struct scene_obj obj;

    memset(&s_scene, 0, sizeof(s_scene));
    memset(&obj, 0, sizeof(obj));
    s_scene.zoom = 1.0;
    s_scene.focal_x = 100;
    s_scene.focal_y = 100;
    s_scene.near_ofs = 1;
    s_scene.obj_count = 1;
    s_scene.center_x = 160;
    s_scene.center_y = 100;
    obj.mesh = triangle;
    obj.self = &obj;
    s_visible[0] = &obj;
    g_scene = &s_scene;
    g_scene_visible = s_visible;
}

int main(void)
{
    unsigned away_color, away;
    unsigned view_target;
    int away_bind, resume_bind, discard_at, last_draw;
    int targets_before;
    int i;

    prepare_scene();
    reset_records();
    g_two_players = 0;
    r_552f58 = 0;

    /* No 3D view was started (a menu or empty frame): a final end must
     * not invalidate anything. */
    gl3d_view_end(1);
    CHECK(s_event_count == 0);
    CHECK(s_layer_calls == 0);

    /* One view: its depth is discarded after its last draw, while its
     * target is still bound. */
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width, 1);
    check_frame(EXPECT_DISCARDS, "single view");

    /* The 2D drawing's target after the final view: a real one, so the
     * recorder accepts the bind that ends the view's lifetime. It has no
     * depth and must never be the discard's target. */
    away_color = rnd_texture(RND_RG8, 1, 1, NULL, 0);
    away = rnd_target(away_color, 0);
    rnd_bind(away);
    CHECK(event_index(EVENT_DISCARD, away, 0) < 0);

    /* Split screen: the first half ends with first=1 but is resumed by
     * the second half, so its depth stays. The HUD in between binds 2D
     * targets; only the final half discards. */
    reset_records();
    g_two_players = 1;
    r_552f58 = 0;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, g_screen_width / 2,
                   g_screen_width, 1);
    check_frame(0, "split first half");
    for (i = 0; i < s_event_count; i++) {
        if (s_events[i].kind == EVENT_BIND) {
            view_target = s_events[i].target;
            break;
        }
    }
    rnd_bind(away);
    away_bind = s_event_count - 1;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width / 2, 0);
    check_frame(EXPECT_DISCARDS, "split final half");

    /* The resumed view binds the same framebuffer after the 2D target,
     * and any discard follows that resume and the final draws. */
    resume_bind = event_index(EVENT_BIND, view_target, away_bind + 1);
    CHECK(resume_bind > away_bind);
    last_draw = -1;
    discard_at = -1;
    for (i = 0; i < s_event_count; i++) {
        if (s_events[i].kind == EVENT_DRAW) {
            last_draw = i;
        } else if (s_events[i].kind == EVENT_DISCARD) {
            discard_at = i;
        }
    }
    CHECK(last_draw > resume_bind);
    if (EXPECT_DISCARDS) {
        CHECK(discard_at > last_draw);
    } else {
        CHECK(discard_at < 0);
    }

    /* The screen went back to one view with two players still set: it is
     * final. */
    reset_records();
    r_552f58 = 1;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width, 1);
    check_frame(EXPECT_DISCARDS, "back to one view");

    /* A second single-view frame discards exactly once again. */
    reset_records();
    g_two_players = 0;
    r_552f58 = 0;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width, 1);
    check_frame(EXPECT_DISCARDS, "repeated frame");

    /* An empty drawable draws no 3D and discards nothing. */
    reset_records();
    s_drawable_w = 0;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width, 1);
    CHECK(s_event_count == 0);
    CHECK(s_layer_calls == 0);
    s_drawable_w = 320;

    /* A resized view recreates its target and depth, then discards the
     * new depth on its final view. */
    reset_records();
    targets_before = s_total_targets;
    s_drawable_w = 480;
    s_drawable_h = 300;
    s_internal_w = 320;
    s_internal_h = 150;
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, 0, g_screen_width, 1);
    check_frame(EXPECT_DISCARDS, "resized view");
    CHECK(s_total_targets == targets_before + 1);

    if (s_failures != 0) {
        fprintf(stderr, "test_discard_depth: %d failures; events=%d\n", s_failures,
                s_event_count);
        puts("FAIL");
        return 1;
    }
    puts("ok");
    return 0;
}
