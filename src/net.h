/*
 * net.h - network play: the session and its players (0x459d30..0x45acd0),
 * and the game's messages in the front end and the race
 * (0x40ce90..0x40e690).
 *
 * The original plays over DirectPlay 3 with its IPX service provider;
 * dplay.c stands in for it on UDP. docs/network.md describes the
 * messages and how to play.
 */

#ifndef IGN_NET_H
#define IGN_NET_H

#include <stddef.h>

#include "types.h"

/* A network game's state, 0x10 bytes: the front end's at 0x4be710 and
 * the race's at 0x552f10. */
struct net_game {
    s32 mode;       /* +0x00: 2 none, 3 hosting, 4 joined (g_net_state) */
    s32 f04;        /* +0x04, unused */
    s32 stage;      /* +0x08: in the front end 0, 1 a client the host
                       answered or the host starting, 2 start the race;
                       in the race 2 waiting for the players, 3 all
                       there, 4 racing */
    s32 players;    /* +0x0c, the players the host has (the front end's) */
};

_Static_assert(offsetof(struct net_game, mode) == 0x00, "net_game.mode");
_Static_assert(offsetof(struct net_game, stage) == 0x08, "net_game.stage");
_Static_assert(offsetof(struct net_game, players) == 0x0c, "net_game.players");
_Static_assert(sizeof(struct net_game) == 0x10, "net_game");

enum {
    NET_MODE_NONE = 2,
    NET_MODE_HOST = 3,
    NET_MODE_CLIENT = 4
};

extern s32 g_net_players;           /* 0x50f7a8, the other players */

/* The command line's choice for the NETWORK GAME item, in place of the
 * original's DirectPlay dialog: `host` hosts a session; `address`
 * (a host name or address, with :port or not) joins the one there.
 * Without either the game joins the first session it finds on the
 * network, or hosts one when there is none. */
void net_set_option(int host, const char *address);

int net_shutdown(void);             /* 0x459d30 */
int net_connect(void);              /* 0x459d40 */
int net_stop(void);                 /* 0x459e10 */
int net_poll_players(void);         /* 0x459e80 */
int net_45a130(void);               /* 0x45a130 */
int net_send(s32 to, const void *buf, u32 size);                  /* 0x45a140 */
int net_receive(s32 *from, void *buf, u32 *size, int any);        /* 0x45a1f0 */

void net_reset(struct net_game *game);                            /* 0x40ce90 */
int net_menu(struct net_game *game, u8 *settings);                /* 0x40cec0 */
int net_send_settings(struct net_game *game, u8 *settings);       /* 0x40d200 */
void net_wait_ready(struct net_game *game);                       /* 0x40d3d0 */
void net_wait_start(struct net_game *game);                       /* 0x40d5c0 */
struct car;
struct car_phys;
void net_receive_cars(struct net_game *game, struct car *cars, struct car_phys *phys,
                      double clock);                              /* 0x40d7a0 */
void net_send_cars(struct net_game *game, struct car *cars, struct car_phys *phys);  /* 0x40e0a0 */
s32 net_slot(s32 i);                                              /* 0x40e690 */

/* race_load_world (0x418d40) clears the players' answers. */
void net_clear_answers(void);

#endif /* IGN_NET_H */
