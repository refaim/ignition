/*
 * dplay.h - a stand-in for the DirectPlay 3 that IGN_WIN.EXE races
 * over, on UDP (platform.h, plat_net_*).
 *
 * The original makes an IDirectPlay2A object for the IPX service
 * provider and calls its methods; net.c keeps those calls, here as
 * dp_* functions with the same arguments, flags, results and system
 * messages, so its error handling is the original's. Only what the
 * game uses is here: one session, players with ids and names, and
 * messages that are not guaranteed to arrive. The session's own
 * protocol is this port's; docs/network.md describes it.
 */

#ifndef IGN_DPLAY_H
#define IGN_DPLAY_H

#include "types.h"

/* HRESULTs, with DirectPlay's values. */
#define DP_OK                   0x00000000u
#define DPERR_BUFFERTOOSMALL    0x8877001eu
#define DPERR_CANTCREATEPLAYER  0x8877003cu
#define DPERR_CANTCREATESESSION 0x88770046u
#define DPERR_INVALIDOBJECT     0x88770082u
#define DPERR_INVALIDPARAMS     0x80070057u
#define DPERR_INVALIDPLAYER     0x88770096u
#define DPERR_NOCONNECTION      0x887700aau
#define DPERR_NOMESSAGES        0x887700beu
#define DPERR_NOSESSIONS        0x887700dcu
#define DPERR_SENDTOOBIG        0x887700e6u
#define DPERR_UNAVAILABLE       0x887700fau
#define DPERR_BUSY              0x8877010eu

#define DPID_SYSMSG             0u      /* the sender of system messages */
#define DPID_ALLPLAYERS         0u

#define DPOPEN_JOIN             1u
#define DPOPEN_CREATE           2u
#define DPRECEIVE_ALL           1u
#define DPRECEIVE_TOPLAYER      2u
#define DPRECEIVE_FROMPLAYER    4u
#define DPSEND_HIGHPRIORITY     8u      /* the game's sends: not guaranteed */
#define DPENUMPLAYERS_LOCAL     0x08u
#define DPENUMPLAYERS_REMOTE    0x10u
#define DPENUMSESSIONS_AVAILABLE 1u
#define DPESC_TIMEDOUT          1u
#define DPCAPS_ISHOST           2u
#define DPPLAYERTYPE_PLAYER     1u
#define DPSESSION_MIGRATEHOST   0x04u
#define DPSESSION_KEEPALIVE     0x40u

/* System messages' dwType. */
#define DPSYS_CREATEPLAYERORGROUP  0x0003u
#define DPSYS_DESTROYPLAYERORGROUP 0x0005u
#define DPSYS_SESSIONLOST          0x0031u
#define DPSYS_HOST                 0x0101u

enum {
    DP_MAX_PLAYERS = 0x17,              /* the game's session size */
    DP_MAX_MESSAGE = 0x400,             /* the game's receive buffers */
    DP_NAME_SIZE = 32,
    DP_PORT = 47624                     /* DirectPlay's own */
};

typedef u32 dp_result;
typedef u32 dpid;

/* DPCAPS, 0x28 bytes. */
struct dp_caps {
    u32 size;                           /* +0x00, 0x28 */
    u32 flags;                          /* +0x04, DPCAPS_ISHOST */
    u32 max_buffer_size;                /* +0x08 */
    u32 max_queue_size;                 /* +0x0c */
    u32 max_players;                    /* +0x10 */
    u32 hundred_baud;                   /* +0x14 */
    u32 latency;                        /* +0x18 */
    u32 max_local_players;              /* +0x1c */
    u32 header_length;                  /* +0x20 */
    u32 timeout;                        /* +0x24 */
};

/* DPSESSIONDESC2, cut to what the game fills in. The GUIDs become the
 * host's session number; the application is always Ignition. */
struct dp_session_desc {
    u32 flags;
    u32 instance;                       /* guidInstance */
    u32 max_players;
    u32 current_players;
    char name[DP_NAME_SIZE];
};

/* DPNAME */
struct dp_name {
    u32 size;
    u32 flags;
    const char *short_name;
    const char *long_name;
};

typedef int (*dp_enum_sessions_fn)(const struct dp_session_desc *desc, u32 *timeout, u32 flags,
                                   void *context);
typedef int (*dp_enum_players_fn)(dpid id, u32 type, const struct dp_name *name, u32 flags,
                                  void *context);

/* DirectPlay answers the network on its own threads; the port calls
 * this once a frame instead (app_tick), so a host is found and its
 * players kept alive on any page of the menu. Messages wait in the
 * queue for dp_receive. */
void dp_pump(void);

/* DirectPlayCreate and QueryInterface: DPERR_UNAVAILABLE without UDP. */
dp_result dp_create(void);
/* Release */
void dp_release(void);
/* EnumSessions: asks the network, and `address` (a host name or
 * address, or NULL) as well, for sessions for `timeout` milliseconds,
 * and gives each to `fn`, then calls it once more with DPESC_TIMEDOUT. */
dp_result dp_enum_sessions(const char *address, u32 timeout, dp_enum_sessions_fn fn,
                           void *context, u32 flags);
/* Open: DPOPEN_CREATE makes a session from `desc`; DPOPEN_JOIN joins
 * the one dp_enum_sessions found with desc->instance. */
dp_result dp_open(const struct dp_session_desc *desc, u32 flags);
dp_result dp_close(void);
dp_result dp_create_player(dpid *id, const struct dp_name *name);
dp_result dp_destroy_player(dpid id);
dp_result dp_enum_players(dp_enum_players_fn fn, void *context, u32 flags);
dp_result dp_get_caps(struct dp_caps *caps, u32 flags);
dp_result dp_get_player_caps(dpid id, struct dp_caps *caps, u32 flags);
dp_result dp_send(dpid from, dpid to, u32 flags, const void *data, u32 size);
/* Receive: the oldest message; *from is DPID_SYSMSG for a system
 * message. DPERR_NOMESSAGES when there is none. */
dp_result dp_receive(dpid *from, dpid *to, u32 flags, void *data, u32 *size);

#endif /* IGN_DPLAY_H */
