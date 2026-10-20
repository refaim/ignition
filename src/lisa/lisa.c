/*
 * lisa.c - start up and shut down of the Lisa 2 library.
 */

#include "lisa/exit.h"
#include "lisa/lisa.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "lisa/texture.h"
#include "stubs.h"
#include "video.h"

/* 0x45e610
 * Clears the headers of seven object tables (0x51fe98, 0x5203b8,
 * 0x520360, 0x51fe30, 0x51fc88, 0x5203a0 and the texture table at
 * 0x520380), then sets each up. */
void render_init(void)
{
    render_init_45f490();
    render_init_460740();
    tex_init();
    render_init_4614d0();
    render_init_4607c0();
    render_init_45f640();
    render_init_45fa50();
    render_init_460b50();
}

/* 0x45b170 */
int lisa_init(void)
{
    log_banner();
    mem_init();
    exit_init();
    input_reset();
    render_init();
    video_select_driver(0);
    return 1;
}

/* 0x45b140 */
static int lisa_free_pools(void)
{
    int i;

    for (i = 0; i < MEM_MAX_POOLS; i++) {
        if (mem_pool_exists(i)) {
            mem_pool_destroy(i);
        }
    }
    return 1;
}

/* 0x45b1a0 */
int lisa_shutdown(void)
{
    exit_run_all();
    lisa_free_pools();
    return 1;
}
