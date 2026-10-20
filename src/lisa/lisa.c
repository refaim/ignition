/*
 * lisa.c - start up and shut down of the Lisa 2 library.
 */

#include "lisa/lisa.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "stubs.h"
#include "video.h"

/* 0x45b170 */
int lisa_init(void)
{
    log_banner();
    mem_init();
    lisa_tasks_init();
    input_reset();
    render_init();
    video_select_driver(0);
    return 1;
}

/* 0x45b1a0 */
int lisa_shutdown(void)
{
    lisa_tasks_shutdown();
    lisa_lib_shutdown();
    return 1;
}
