/*
 * test_effect_wreck.c - the wreck's skid flags and the active-car global.
 *
 * The plant is the real libignition.so.  race_effects_tick dispatches the
 * real type 3, 7 and 10 effects; the age-zero path reaches the real
 * wreck_start.  The fixture does not replace wreck_start, the dispatch or
 * the effect algorithms.
 *
 * The bug: wreck_start(c) stops c's skid voices, then clears
 * g_cars[r_5285c0].f298/f29c instead of c->f298/f29c.  race.c leaves
 * r_5285c0 == g_driver_count when it calls race_effects_tick.  Therefore an
 * outgoing flag stays set and an unrelated car (or memory just past the
 * active cars) is cleared.
 *
 * In the default matrix, three active cars are exposed through g_cars.
 * One full-car guard sits on each side of that active range.  The guards
 * are allocated storage, not additional active cars: g_cars[-1] and
 * g_cars[g_driver_count].
 *
 * --asan-exact instead allocates exactly three cars and performs no guard
 * reads.  With an instrumented DSO, the old g_cars[g_driver_count] write
 * is a heap-buffer-overflow at the real production instruction.
 *
 * Only sound, GL and animated-texture boundaries are interposed.  The
 * dummy scene objects have in_grid == 0, so the production car_objects
 * path does not need the scene graph.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim_tex.h"
#include "gl3d.h"
#include "race/cars.h"
#include "race/contact.h"
#include "race/data.h"
#include "race/fx.h"
#include "race/init.h"
#include "race/race.h"
#include "scene.h"
#include "sound.h"

enum {
    TEST_DRIVERS = 3,
    GUARD_CARS = 2,
    STORAGE_CARS = TEST_DRIVERS + GUARD_CARS,
    VOICE_PAIRS = TEST_DRIVERS * 2
};

enum handle_mode {
    HANDLES_VALID,
    HANDLES_ABSENT,
    HANDLES_STALE
};

struct effect_case {
    int type;
    int target;
    int f298;
    int f29c;
    int age;
};

struct test_case {
    const char *name;
    int effect_count;
    struct effect_case effects[2];
    int incoming;
    int handles;
};

struct fake_voice {
    int car;
    int axle;
    s32 handle;
    struct sound_voice voice;
};

/* src/race/fx.c and src/race/init.c define these without a header. */
extern float r_563d64;
extern s32 *r_552f24;
extern s32 r_5285d0;
extern struct scene_desc *g_part_objs;

static struct fake_voice s_voices[VOICE_PAIRS];
static int s_voice_count;
static int s_lookups;
static int s_dynamic_calls;
static int s_table_calls;
static int s_anim_steps;
static int s_failures;
static int s_case_failures;
static const char *s_case_name;

static struct car *s_storage;
static struct race_driver *s_drivers;
static struct scene_desc *s_car_objs;
static struct scene_desc *s_wheel_objs;
static struct scene_desc *s_shadow_objs;
static struct scene_desc *s_car7_objs;
static struct race_cam *s_cams;
static struct scene_desc *s_part_objs;

static void fail_int(const char *what, int got, int want)
{
    fprintf(stderr, "FAIL [%s]: %s = %d, want %d\n", s_case_name, what, got, want);
    s_failures++;
    s_case_failures++;
}

static void fail_text(const char *what)
{
    fprintf(stderr, "FAIL [%s]: %s\n", s_case_name, what);
    s_failures++;
    s_case_failures++;
}

/* Sound boundary: only the fixture's voice records are visible. */
__attribute__((visibility("default")))
struct sound_voice *sound_voice_of(s32 handle)
{
    int i;

    s_lookups++;
    if (handle == 0) {
        return NULL;
    }
    for (i = 0; i < s_voice_count; i++) {
        if (s_voices[i].handle == handle) {
            return &s_voices[i].voice;
        }
    }
    return NULL;
}

/* Sound boundary: effect sounds have no mixer in this process. */
__attribute__((visibility("default")))
s32 race_sound_3d(int bank, int group, int sample, int loops, int volume, int rate, int pan,
                  float x, float z)
{
    (void)bank;
    (void)group;
    (void)sample;
    (void)loops;
    (void)volume;
    (void)rate;
    (void)pan;
    (void)x;
    (void)z;
    return 0;
}

/* Render boundary: race_effects_tick announces its dynamic mesh range. */
__attribute__((visibility("default")))
void gl3d_set_dynamic(const s32 *begin, const s32 *end)
{
    (void)begin;
    (void)end;
    s_dynamic_calls++;
}

/* Render boundary: the level tables are irrelevant to this state test. */
__attribute__((visibility("default")))
void gl3d_set_tables(const u8 *shd, const u8 *fixed)
{
    (void)shd;
    (void)fixed;
    s_table_calls++;
}

/*
 * Render boundary: each case resets r_563d64 to 0.  For g_track 0 the
 * confirmed g_anim_ticks[0] is 6, so the tick's increment to 1 must not
 * step an animated texture.  Counting here preserves that assertion.
 */
__attribute__((visibility("default")))
void anim_tex_step(void)
{
    s_anim_steps++;
}

/* Scene boundary: effect meshes are not linked into a live scene here. */
__attribute__((visibility("default")))
int scene_obj_set_mesh(struct scene_desc *desc, const s32 *mesh, int tex, int klass, int f20,
                       int radius, int group)
{
    (void)desc;
    (void)mesh;
    (void)tex;
    (void)klass;
    (void)f20;
    (void)radius;
    (void)group;
    return 0;
}

__attribute__((visibility("default")))
int scene_obj_link(struct scene_desc *desc)
{
    (void)desc;
    return 0;
}

__attribute__((visibility("default")))
int scene_obj_move(struct scene_desc *desc)
{
    (void)desc;
    return 0;
}

__attribute__((visibility("default")))
int scene_obj_unlink(struct scene_desc *desc)
{
    (void)desc;
    return 0;
}

/* Render boundary: no mesh cache exists in this process. */
__attribute__((visibility("default")))
void gl3d_mesh_changed(const s32 *mesh)
{
    (void)mesh;
}

static int init_environment(int exact_cars)
{
    s_storage = calloc(exact_cars ? TEST_DRIVERS : STORAGE_CARS, sizeof(*s_storage));
    s_drivers = calloc(TEST_DRIVERS, sizeof(*s_drivers));
    s_car_objs = calloc(TEST_DRIVERS, sizeof(*s_car_objs));
    s_wheel_objs = calloc(TEST_DRIVERS * 4, sizeof(*s_wheel_objs));
    s_shadow_objs = calloc(TEST_DRIVERS, sizeof(*s_shadow_objs));
    s_car7_objs = calloc(TEST_DRIVERS, sizeof(*s_car7_objs));
    s_cams = calloc(TEST_DRIVERS, sizeof(*s_cams));
    s_part_objs = calloc(FX_SLOTS, sizeof(*s_part_objs));
    if (s_storage == NULL || s_drivers == NULL || s_car_objs == NULL
        || s_wheel_objs == NULL || s_shadow_objs == NULL || s_car7_objs == NULL
        || s_cams == NULL || s_part_objs == NULL) {
        fprintf(stderr, "test_effect_wreck: out of memory\n");
        return 0;
    }

    /* The default guards are storage only; g_cars exposes three cars. */
    g_cars = exact_cars ? s_storage : &s_storage[1];
    g_drivers = s_drivers;
    g_car_objs = s_car_objs;
    g_wheel_objs = s_wheel_objs;
    g_shadow_objs = s_shadow_objs;
    g_car7_objs = s_car7_objs;
    g_race_cams = s_cams;
    g_part_objs = s_part_objs;

    g_driver_count = TEST_DRIVERS;
    g_view_car = 0;
    g_two_players = 0;
    g_race_mode = 0;
    g_track = 0;
    memset(&g_race_net, 0, sizeof(g_race_net));
    r_54f980 = -1.0;
    r_54f930 = 0;
    r_54f954 = 0;
    r_563cf0 = 0;
    r_563d78 = 0;
    r_552f78 = 0;
    r_552f24 = NULL;
    r_5285d0 = 0;
    r_5532b0 = 0;
    r_563d64 = 0.0f;
    g_level_shd = NULL;
    g_level_pan = NULL;
    return 1;
}

static void free_environment(void)
{
    free(s_storage);
    free(s_drivers);
    free(s_car_objs);
    free(s_wheel_objs);
    free(s_shadow_objs);
    free(s_car7_objs);
    free(s_cams);
    free(s_part_objs);
    s_storage = NULL;
    s_drivers = NULL;
    s_car_objs = NULL;
    s_wheel_objs = NULL;
    s_shadow_objs = NULL;
    s_car7_objs = NULL;
    s_cams = NULL;
    s_part_objs = NULL;
    g_cars = NULL;
    g_drivers = NULL;
    g_car_objs = NULL;
    g_wheel_objs = NULL;
    g_shadow_objs = NULL;
    g_car7_objs = NULL;
    g_race_cams = NULL;
    g_part_objs = NULL;
}

static s32 install_voice(int car, int axle)
{
    struct fake_voice *v = &s_voices[s_voice_count++];

    v->car = car;
    v->axle = axle;
    v->handle = (s32)(0x00010001u + (unsigned)s_voice_count * 3u);
    v->voice.rate = 41000 + car * 100 + axle * 10;
    v->voice.volume = 30000 + car * 100 + axle * 10;
    return v->handle;
}

static int flag_for(const struct effect_case *e, int axle)
{
    return axle == 0 ? e->f298 : e->f29c;
}

static int effect_is_target(const struct test_case *c, int car)
{
    int i;

    for (i = 0; i < c->effect_count; i++) {
        if (c->effects[i].target == car) {
            return 1;
        }
    }
    return 0;
}

static int expected_old_pass(const struct test_case *c)
{
    int i;

    if (c->effect_count != 1) {
        return 0;
    }
    for (i = 0; i < c->effect_count; i++) {
        const struct effect_case *e = &c->effects[i];

        if (e->age == 0 && (e->f298 == 1 || e->f29c == 1)) {
            return c->incoming == e->target;
        }
    }
    return 1;
}

static int run_case(const struct test_case *c)
{
    struct car active_before[TEST_DRIVERS];
    struct car left_before;
    struct car right_before;
    struct sound_voice voices_before[VOICE_PAIRS];
    int expected_stop[TEST_DRIVERS][2];
    int expected_lookups = 0;
    int special_target = c->effects[0].target;
    int i, car, axle;

    s_case_name = c->name;
    s_case_failures = 0;
    s_lookups = 0;
    s_dynamic_calls = 0;
    s_table_calls = 0;
    s_anim_steps = 0;
    s_voice_count = 0;
    r_563d64 = 0.0f;

    memset(s_storage, 0xA5, STORAGE_CARS * sizeof(*s_storage));
    memset(s_drivers, 0, TEST_DRIVERS * sizeof(*s_drivers));
    memset(s_car_objs, 0, TEST_DRIVERS * sizeof(*s_car_objs));
    memset(s_wheel_objs, 0, TEST_DRIVERS * 4 * sizeof(*s_wheel_objs));
    memset(s_shadow_objs, 0, TEST_DRIVERS * sizeof(*s_shadow_objs));
    memset(s_car7_objs, 0, TEST_DRIVERS * sizeof(*s_car7_objs));
    memset(s_cams, 0, TEST_DRIVERS * sizeof(*s_cams));
    memset(s_part_objs, 0, FX_SLOTS * sizeof(*s_part_objs));
    memset(g_effects, 0, sizeof(g_effects));
    memset(&g_effect, 0, sizeof(g_effect));
    memset(expected_stop, 0, sizeof(expected_stop));

    for (car = 0; car < TEST_DRIVERS; car++) {
        struct car *t = &g_cars[car];
        int f298 = 1, f29c = 1;
        s32 rear, front;

        memset(t, 0, sizeof(*t));
        for (i = 0; i < c->effect_count; i++) {
            if (c->effects[i].target == car) {
                f298 = c->effects[i].f298;
                f29c = c->effects[i].f29c;
            }
        }
        t->x = 100.0 + car;
        t->y = 10.0 + car;
        t->z = 200.0 + car;
        t->heading = 0.125 * car;
        t->f298 = f298;
        t->f29c = f29c;

        rear = install_voice(car, 0);
        front = install_voice(car, 1);
        if (car == special_target && c->handles != HANDLES_VALID) {
            rear = c->handles == HANDLES_ABSENT ? 0 : 0x7f7f7f7f;
            front = rear;
        }
        t->skid_voice_rear = rear;
        t->skid_voice_front = front;
    }

    for (i = 0; i < c->effect_count; i++) {
        const struct effect_case *e = &c->effects[i];
        struct race_effect *slot = &g_effects[i];
        const struct car *target = &g_cars[e->target];

        if (e->age == 0) {
            for (axle = 0; axle < 2; axle++) {
                if (flag_for(e, axle) == 1) {
                    int special = e->target == special_target
                                  && c->handles != HANDLES_VALID;

                    expected_lookups++;
                    if (!special) {
                        expected_stop[e->target][axle] = 1;
                    }
                }
            }
        }
        slot->type = e->type;
        slot->x = (s32)(target->x * 1024.0);
        slot->y = (s32)(target->y * 1024.0);
        slot->z = (s32)(target->z * 1024.0);
        slot->f10 = e->target;
        slot->f14 = e->age;
        slot->f18 = 100;
    }

    for (car = 0; car < TEST_DRIVERS; car++) {
        active_before[car] = g_cars[car];
    }
    left_before = g_cars[-1];
    right_before = g_cars[g_driver_count];
    for (i = 0; i < s_voice_count; i++) {
        voices_before[i] = s_voices[i].voice;
    }

    r_5285c0 = c->incoming;
    race_effects_tick();

    if (s_dynamic_calls != 1) {
        fail_int("gl3d_set_dynamic calls", s_dynamic_calls, 1);
    }
    if (s_table_calls != 1) {
        fail_int("gl3d_set_tables calls", s_table_calls, 1);
    }
    if (s_anim_steps != 0) {
        fail_int("anim_tex_step calls", s_anim_steps, 0);
    }
    if (s_lookups != expected_lookups) {
        fail_int("sound_voice_of lookups", s_lookups, expected_lookups);
    }
    if (r_5285c0 != c->incoming) {
        fail_int("r_5285c0 after tick", (int)r_5285c0, c->incoming);
    }
    if (memcmp(&g_cars[-1], &left_before, sizeof(left_before)) != 0) {
        fail_text("left guard changed (g_cars[-1], storage outside active cars)");
    }
    if (memcmp(&g_cars[g_driver_count], &right_before, sizeof(right_before)) != 0) {
        fail_text("right guard changed (g_cars[g_driver_count], storage outside active cars)");
    }

    for (car = 0; car < TEST_DRIVERS; car++) {
        if (!effect_is_target(c, car)
            && memcmp(&g_cars[car], &active_before[car], sizeof(g_cars[car])) != 0) {
            fail_text("an active non-target car changed");
        }
    }

    for (i = 0; i < c->effect_count; i++) {
        const struct effect_case *e = &c->effects[i];
        int want298 = e->age == 0 && e->f298 == 1 ? 0 : e->f298;
        int want29c = e->age == 0 && e->f29c == 1 ? 0 : e->f29c;
        int want270 = e->age == 0 ? 1 : active_before[e->target].f270;
        char what[72];

        if (g_cars[e->target].f298 != want298) {
            snprintf(what, sizeof(what), "car %d f298", e->target);
            fail_int(what, g_cars[e->target].f298, want298);
        }
        if (g_cars[e->target].f29c != want29c) {
            snprintf(what, sizeof(what), "car %d f29c", e->target);
            fail_int(what, g_cars[e->target].f29c, want29c);
        }
        if (g_cars[e->target].f270 != want270) {
            snprintf(what, sizeof(what), "car %d f270", e->target);
            fail_int(what, g_cars[e->target].f270, want270);
        }
    }

    for (i = 0; i < s_voice_count; i++) {
        struct sound_voice want = voices_before[i];

        if (expected_stop[s_voices[i].car][s_voices[i].axle]) {
            want.rate = 0;
        }
        if (memcmp(&s_voices[i].voice, &want, sizeof(want)) != 0) {
            char what[80];

            snprintf(what, sizeof(what), "voice car %d axle %d rate/record",
                     s_voices[i].car, s_voices[i].axle);
            fail_int(what, s_voices[i].voice.rate, want.rate);
        }
    }

    printf("%-62s %s %s\n", c->name, s_case_failures == 0 ? "ok" : "FAILED",
           expected_old_pass(c) ? "[old-control]" : "[RED-before-fix]");
    return s_case_failures;
}

/*
 * One type 3, age zero, car 1, both flags 1, incoming g_driver_count.
 * g_cars is an allocation of exactly three struct car records: there are
 * no guards and this function never reads outside that allocation.  An
 * ASan-instrumented buggy DSO must stop at the real g_cars[3].f298 write.
 */
static int run_asan_exact_case(void)
{
    struct car active_before[TEST_DRIVERS];
    struct sound_voice voices_before[VOICE_PAIRS];
    int i, car;

    s_case_name = "ASAN exact type3 car1 flags 1/1 incoming driver_count";
    s_case_failures = 0;
    s_lookups = 0;
    s_dynamic_calls = 0;
    s_table_calls = 0;
    s_anim_steps = 0;
    s_voice_count = 0;
    r_563d64 = 0.0f;

    memset(s_storage, 0, TEST_DRIVERS * sizeof(*s_storage));
    memset(s_drivers, 0, TEST_DRIVERS * sizeof(*s_drivers));
    memset(s_car_objs, 0, TEST_DRIVERS * sizeof(*s_car_objs));
    memset(s_wheel_objs, 0, TEST_DRIVERS * 4 * sizeof(*s_wheel_objs));
    memset(s_shadow_objs, 0, TEST_DRIVERS * sizeof(*s_shadow_objs));
    memset(s_car7_objs, 0, TEST_DRIVERS * sizeof(*s_car7_objs));
    memset(s_cams, 0, TEST_DRIVERS * sizeof(*s_cams));
    memset(s_part_objs, 0, FX_SLOTS * sizeof(*s_part_objs));
    memset(g_effects, 0, sizeof(g_effects));
    memset(&g_effect, 0, sizeof(g_effect));
    memset(s_voices, 0, sizeof(s_voices));

    for (car = 0; car < TEST_DRIVERS; car++) {
        struct car *t = &g_cars[car];

        t->x = 100.0 + car;
        t->y = 10.0 + car;
        t->z = 200.0 + car;
        t->heading = 0.125 * car;
        t->f298 = 1;
        t->f29c = 1;
        t->skid_voice_rear = install_voice(car, 0);
        t->skid_voice_front = install_voice(car, 1);
    }

    g_effects[0].type = 3;
    g_effects[0].x = (s32)(g_cars[1].x * 1024.0);
    g_effects[0].y = (s32)(g_cars[1].y * 1024.0);
    g_effects[0].z = (s32)(g_cars[1].z * 1024.0);
    g_effects[0].f10 = 1;
    g_effects[0].f14 = 0;
    g_effects[0].f18 = 100;

    for (car = 0; car < TEST_DRIVERS; car++) {
        active_before[car] = g_cars[car];
    }
    for (i = 0; i < s_voice_count; i++) {
        voices_before[i] = s_voices[i].voice;
    }

    r_5285c0 = TEST_DRIVERS;
    race_effects_tick();

    if (s_dynamic_calls != 1) {
        fail_int("gl3d_set_dynamic calls", s_dynamic_calls, 1);
    }
    if (s_table_calls != 1) {
        fail_int("gl3d_set_tables calls", s_table_calls, 1);
    }
    if (s_anim_steps != 0) {
        fail_int("anim_tex_step calls", s_anim_steps, 0);
    }
    if (s_lookups != 2) {
        fail_int("sound_voice_of lookups", s_lookups, 2);
    }
    if (r_5285c0 != TEST_DRIVERS) {
        fail_int("r_5285c0 after tick", (int)r_5285c0, TEST_DRIVERS);
    }
    if (g_cars[1].f298 != 0) {
        fail_int("car 1 f298", g_cars[1].f298, 0);
    }
    if (g_cars[1].f29c != 0) {
        fail_int("car 1 f29c", g_cars[1].f29c, 0);
    }
    if (g_cars[1].f270 != 1) {
        fail_int("car 1 f270", g_cars[1].f270, 1);
    }
    for (car = 0; car < TEST_DRIVERS; car++) {
        if (car != 1 && memcmp(&g_cars[car], &active_before[car], sizeof(g_cars[car])) != 0) {
            fail_text("an active non-target car changed");
        }
    }
    for (i = 0; i < s_voice_count; i++) {
        struct sound_voice want = voices_before[i];

        if (s_voices[i].car == 1) {
            want.rate = 0;
        }
        if (memcmp(&s_voices[i].voice, &want, sizeof(want)) != 0) {
            char what[80];

            snprintf(what, sizeof(what), "voice car %d axle %d rate/record",
                     s_voices[i].car, s_voices[i].axle);
            fail_int(what, s_voices[i].voice.rate, want.rate);
        }
    }

    printf("%-62s %s [asan-exact-min-red]\n", s_case_name,
           s_case_failures == 0 ? "ok" : "FAILED");
    return s_case_failures;
}

int main(int argc, char **argv)
{
    static const struct test_case min_red = {
        .name = "MIN RED type3 car1 flags 1/1 incoming driver_count",
        .effect_count = 1,
        .effects = {{ .type = 3, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
        .incoming = TEST_DRIVERS,
        .handles = HANDLES_VALID
    };
    static const struct test_case specials[] = {
        {
            .name = "special type3 absent handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 3, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_ABSENT
        },
        {
            .name = "special type7 absent handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 7, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_ABSENT
        },
        {
            .name = "special type10 absent handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 10, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_ABSENT
        },
        {
            .name = "special type3 stale handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 3, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_STALE
        },
        {
            .name = "special type7 stale handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 7, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_STALE
        },
        {
            .name = "special type10 stale handles flags 1/1",
            .effect_count = 1,
            .effects = {{ .type = 10, .target = 1, .f298 = 1, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_STALE
        },
        {
            .name = "special type3 nonbinary 2/1 at incoming driver_count",
            .effect_count = 1,
            .effects = {{ .type = 3, .target = 1, .f298 = 2, .f29c = 1, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_VALID
        },
        {
            .name = "special type7 nonbinary 1/2 at incoming driver_count",
            .effect_count = 1,
            .effects = {{ .type = 7, .target = 1, .f298 = 1, .f29c = 2, .age = 0 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_VALID
        },
        {
            .name = "special positive age type3 flags 1/1 no reinit",
            .effect_count = 1,
            .effects = {{ .type = 3, .target = 1, .f298 = 1, .f29c = 1, .age = 1 }},
            .incoming = TEST_DRIVERS,
            .handles = HANDLES_VALID
        },
        {
            .name = "special two effects type3 car1 + type10 car2",
            .effect_count = 2,
            .effects = {
                { .type = 3, .target = 1, .f298 = 1, .f29c = 1, .age = 0 },
                { .type = 10, .target = 2, .f298 = 1, .f29c = 1, .age = 0 }
            },
            .incoming = 2,
            .handles = HANDLES_VALID
        }
    };
    static const int types[] = { 3, 7, 10 };
    static const int incoming[] = { 1, 2, -1, TEST_DRIVERS };
    static const char *const incoming_name[] = { "target", "other", "-1", "driver_count" };
    static const char *const flag_name[] = { "00", "01", "10", "11" };
    int control_total = 0, control_passed = 0;
    int red_total = 0, red_failed = 0;
    int exact_cars = 0;
    int t, flags, inc, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--asan-exact") == 0) {
            exact_cars = 1;
        } else {
            fprintf(stderr, "usage: %s [--asan-exact]\n", argv[0]);
            return 2;
        }
    }

    if (!init_environment(exact_cars)) {
        return 2;
    }

    printf("test_effect_wreck: real race_effects_tick from libignition.so\n");
    if (exact_cars) {
        printf("test_effect_wreck: ASan exact mode, 3-car heap, no guards\n");
        if (run_asan_exact_case() != 0) {
            free_environment();
            printf("test_effect_wreck: ASAN EXACT FAILED, %d failed checks\n", s_failures);
            return 1;
        }
        free_environment();
        printf("test_effect_wreck: ASAN EXACT PASS\n");
        return 0;
    }

    /* Keep the first observable result the smallest useful RED. */
    red_total++;
    if (run_case(&min_red) != 0) {
        red_failed++;
    }

    for (t = 0; t < 3; t++) {
        for (flags = 0; flags < 4; flags++) {
            for (inc = 0; inc < 4; inc++) {
                struct test_case c;
                char name[96];
                int failed;

                snprintf(name, sizeof(name), "matrix type%d flags %s incoming %s",
                         types[t], flag_name[flags], incoming_name[inc]);
                memset(&c, 0, sizeof(c));
                c.name = name;
                c.effect_count = 1;
                c.effects[0].type = types[t];
                c.effects[0].target = 1;
                c.effects[0].f298 = (flags & 1) != 0;
                c.effects[0].f29c = (flags & 2) != 0;
                c.effects[0].age = 0;
                c.incoming = incoming[inc];
                c.handles = HANDLES_VALID;
                failed = run_case(&c);
                if (expected_old_pass(&c)) {
                    control_total++;
                    control_passed += failed == 0;
                } else {
                    red_total++;
                    red_failed += failed != 0;
                }
            }
        }
    }

    for (i = 0; i < (int)(sizeof(specials) / sizeof(specials[0])); i++) {
        int failed = run_case(&specials[i]);

        if (expected_old_pass(&specials[i])) {
            control_total++;
            control_passed += failed == 0;
        } else {
            red_total++;
            red_failed += failed != 0;
        }
    }

    printf("old-control cases passed: %d/%d\n", control_passed, control_total);
    printf("RED-before-fix cases failing: %d/%d\n", red_failed, red_total);

    free_environment();
    if (s_failures != 0) {
        printf("test_effect_wreck: FAILED, %d failed checks\n", s_failures);
        return 1;
    }
    printf("test_effect_wreck: PASS\n");
    return 0;
}
