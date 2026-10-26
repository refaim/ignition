/*
 * test_race_context.c - the active-car global at the contact boundary.
 *
 * The ARMv7 crash was race_obj_query reading g_cars[r_5285c0].deck
 * (src/srf.c:276) from race_contact. race_physics loops a local i and
 * calls the static step_car(i), which assigns r_5285c0 = i before
 * race_contact(i) (src/race/phys.c:413, 421); drive(i) assigns it later
 * (:592). The loops of race_step leave it at g_driver_count
 * (src/race/race.c:185, 225).
 *
 * This test drives the production race_physics -> step_car -> race_contact
 * -> race_obj_query path and asserts, at query entry, that r_5285c0 is the
 * car being collided. The plant is the real libignition.so.
 *
 * Substitutions, so this is not an end-to-end test:
 *   - srf_lookup: the .SRF grid is environmental here; it returns an empty
 *     surface hit.
 *   - race_obj_query: observation boundary. It records and longjmps instead
 *     of running the object search, whose first statement is the
 *     dereference under test.
 * The state-setting mechanism (where r_5285c0 is assigned relative to
 * race_contact) is production code and is not replaced.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "race/cars.h"
#include "race/init.h"
#include "race/phys.h"
#include "race/race.h"
#include "srf.h"

enum { TEST_DRIVERS = 3 };

/* src/race/init.c defines both; no included header names them. */
extern double r_54f948;
extern s32 r_5285d0;

static jmp_buf s_stop;
static struct srf_hit s_empty;
static int s_target;
static int s_queries;
static int s_failures;
static int s_case_failures;

static void query_fail(const char *what, int value, int want)
{
    fprintf(stderr, "FAIL car %d: %s is %d, want %d\n",
            s_target, what, value, want);
    s_failures++;
    s_case_failures++;
}

/* Environmental substitution: this fixture has no .SRF data. */
__attribute__((used))
struct srf_hit *srf_lookup(s32 x, s32 y, s32 z)
{
    (void)x;
    (void)y;
    (void)z;
    s_empty.type = -1;
    return &s_empty;
}

/*
 * Observation boundary, entered from race_contact (src/race/contact.c:514).
 * The production body's first read is g_cars[r_5285c0].deck.
 */
__attribute__((used))
struct srf_hit *race_obj_query(s32 x, s32 y, s32 z, s32 slot, s32 y_ofs)
{
    int observed = (int)r_5285c0;

    s_queries++;
    if (x != 100 + s_target) {
        query_fail("query x", (int)x, 100 + s_target);
    }
    if (y != 0 || z != 0) {
        fprintf(stderr, "FAIL car %d: query y/z %d/%d, want 0/0\n",
                s_target, (int)y, (int)z);
        s_failures++;
        s_case_failures++;
    }
    if (slot != -1 || y_ofs != 50) {
        fprintf(stderr, "FAIL car %d: query slot/y_ofs %d/%d, want -1/50\n",
                s_target, (int)slot, (int)y_ofs);
        s_failures++;
        s_case_failures++;
    }
    if (observed != s_target) {
        query_fail("r_5285c0", observed, s_target);
    }
    longjmp(s_stop, 1);
    return &s_empty;
}

/*
 * One production half step. Cars before `target` are already out, so
 * step_car returns before contact for them (a production early return);
 * `target` is the first car to reach the object query.
 */
static void run_case(const char *seed_name, int seed, int target)
{
    int i;

    memset(g_cars, 0, TEST_DRIVERS * sizeof(*g_cars));
    memset(g_car_phys, 0, TEST_DRIVERS * sizeof(*g_car_phys));
    memset(g_drivers, 0, TEST_DRIVERS * sizeof(*g_drivers));
    for (i = 0; i < TEST_DRIVERS; i++) {
        g_cars[i].x = 100.0 + i;
        g_cars[i].f354 = i < target;
        g_drivers[i].control = 0;   /* local human, not a remote car */
    }

    g_driver_count = TEST_DRIVERS;
    g_race_net.mode = NET_MODE_NONE;    /* is_remote also needs no client mode */
    r_5285d0 = 1;               /* race_contact probes objects (contact.c:513) */
    r_54f948 = 0.0;
    r_5285c0 = seed;
    s_target = target;
    s_queries = 0;
    s_case_failures = 0;

    if (setjmp(s_stop) == 0) {
        race_physics(1.0);
        fprintf(stderr, "FAIL car %d (%s): race_physics returned before the object query\n",
                target, seed_name);
        s_failures++;
        s_case_failures++;
    } else if (s_queries != 1) {
        fprintf(stderr, "FAIL car %d (%s): %d object queries, want 1\n",
                target, seed_name, s_queries);
        s_failures++;
        s_case_failures++;
    }

    printf("%-24s car %d: %s\n", seed_name, target,
           s_case_failures == 0 ? "ok" : "FAILED");
}

int main(void)
{
    int target;

    g_cars = calloc(TEST_DRIVERS, sizeof(*g_cars));
    g_car_phys = calloc(TEST_DRIVERS, sizeof(*g_car_phys));
    g_drivers = calloc(TEST_DRIVERS, sizeof(*g_drivers));
    if (g_cars == NULL || g_car_phys == NULL || g_drivers == NULL) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    s_empty.type = -1;

    /* After race_step's loops r_5285c0 is g_driver_count. */
    for (target = 0; target < TEST_DRIVERS; target++) {
        run_case("stale = driver_count", TEST_DRIVERS, target);
    }
    /* A negative index must not leak into the query either. */
    for (target = 0; target < TEST_DRIVERS; target++) {
        run_case("sentinel = -1", -1, target);
    }
    /* Control: an already-correct context passes even before the fix. */
    for (target = 0; target < TEST_DRIVERS; target++) {
        run_case("valid = target", target, target);
    }

    free(g_cars);
    g_cars = NULL;
    free(g_car_phys);
    g_car_phys = NULL;
    free(g_drivers);
    g_drivers = NULL;

    if (s_failures != 0) {
        printf("test_race_context: FAILED, %d failed checks\n", s_failures);
        return 1;
    }
    printf("test_race_context: PASS\n");
    return 0;
}
