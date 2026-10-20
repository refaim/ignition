/*
 * net.h - network play. The original runs it over DirectPlay; there is
 * no network here yet, so a session never starts and the game's
 * network state stays at rest.
 */

#ifndef IGN_NET_H
#define IGN_NET_H

#include "types.h"

int net_stop(void);
void net_reset(void *state);

#endif /* IGN_NET_H */
