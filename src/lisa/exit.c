/*
 * exit.c - Lisa 2 exit handlers.
 */

#include <string.h>

#include "lisa/exit.h"

enum {
    EXIT_SLOTS = 200,
    EXIT_PRIORITY_FIRST = 0x10000,
    EXIT_PRIORITY_LAST = 0x20000
};

static int s_ready;                         /* 0x4bab38 */
static s32 s_active[EXIT_SLOTS];            /* 0x5116e0 */
static s32 s_priority[EXIT_SLOTS];          /* 0x511d20 */
static const char *s_name[EXIT_SLOTS];      /* 0x510bf0 */
static s32 s_id[EXIT_SLOTS];                /* 0x5113c0 */
static s32 s_user[EXIT_SLOTS];              /* 0x510f10 */
static exit_fn s_fn[EXIT_SLOTS];            /* 0x511a00 */
static s16 s_free_ids[EXIT_SLOTS];          /* 0x511230 */
static s16 s_free_top;                      /* 0x512040 */

/* 0x45b1f0 */
int exit_init(void)
{
    int i;

    if (s_ready == 1) {
        return 1;
    }
    s_ready = 1;
    memset(s_active, 0, sizeof(s_active));
    for (i = 0; i < EXIT_SLOTS; i++) {
        s_free_ids[i] = (s16)(i + 1);
    }
    s_free_top = 0;
    return 1;
}

/* 0x45b1b0 */
int exit_alloc_id(void)
{
    s16 top = s_free_top;

    if (!s_ready || top + 1 >= EXIT_SLOTS) {
        return -1;
    }
    s_free_top = (s16)(top + 1);
    return s_free_ids[top];
}

/* 0x45b360 */
int exit_register(int id, const char *name, exit_fn fn, s32 user)
{
    int i;

    if (!s_ready) {
        return 0;
    }
    for (i = 0; i < EXIT_SLOTS && s_active[i] != 0; i++) {
    }
    if (i == EXIT_SLOTS) {
        return -1;
    }
    s_active[i] = 1;
    s_priority[i] = EXIT_PRIORITY_FIRST;
    s_name[i] = name;
    s_id[i] = id;
    s_user[i] = user;
    s_fn[i] = fn;
    return i;
}

/* 0x45b410 */
int exit_unregister(int id)
{
    int i;

    if (!s_ready) {
        return 0;
    }
    for (i = 0; i < EXIT_SLOTS; i++) {
        if (s_active[i] == 1 && s_id[i] == id) {
            s_active[i] = 0;
        }
    }
    return 1;
}

static void run_pass(s32 priority)
{
    int i;

    for (i = 0; i < EXIT_SLOTS; i++) {
        if (s_active[i] == 1 && s_priority[i] == priority) {
            s_active[i] = 0;
            s_fn[i](s_user[i]);
        }
    }
}

/* 0x45b240 */
int exit_run_all(void)
{
    if (!s_ready) {
        return 1;
    }
    s_ready = 0;
    run_pass(EXIT_PRIORITY_FIRST);
    run_pass(EXIT_PRIORITY_LAST);
    return 1;
}
