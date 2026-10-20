/*
 * net.c - network play (0x459d30..0x45a3c0 and the front end's message
 * state).
 */

#include <string.h>

#include "net.h"
#include "game.h"

static s32 s_session;           /* 0x4bab04, the DirectPlay object */
static s32 s_net_4bab00;        /* 0x4bab00 */

/* The front end's message state. */
static s32 s_messages[0x17];    /* 0x63a470 */
static s32 s_net_63a458;        /* 0x63a458 */
static s32 s_net_63a464;        /* 0x63a464 */
static s32 s_net_63a3dc;        /* 0x63a3dc */
static s32 s_net_63a45c;        /* 0x63a45c */

/* 0x459e10: leaves the session, if hosting or joining one. */
int net_stop(void)
{
    /* The original destroys the player, closes and releases the
     * DirectPlay object here; no session is ever open. */
    s_session = 0;
    g_net_state = 0;
    s_net_4bab00 = 0;
    return 1;
}

/* 0x40ce90 */
void net_reset(void *state)
{
    (void)state;
    memset(s_messages, 0, sizeof(s_messages));
    s_net_63a458 = 0;
    s_net_63a464 = 0;
    s_net_63a3dc = -1;
    s_net_63a45c = 0;
}
