/*
 * net.c - network play: the session and its players (0x459d30..0x45acd0),
 * and the game's messages in the front end and the race
 * (0x40ce90..0x40e690). See net.h, and docs/network.md.
 *
 * The original keeps a table of the other players (up to 0x17, the
 * session's size), by index. Index i is driver i + 1 on the host; a
 * client is told its driver by the host, and swaps it with driver 0,
 * its own car (net_slot). Every message is a block the size of its
 * struct, its first s32 the kind:
 *
 *   1  host to client   "you are driver n"                 8 bytes
 *   3  client to host   in the race: the track is loaded   4 bytes
 *   4  host to client   start                              4 bytes
 *   5  either           a car (struct net_car)          0xc6 bytes
 *   6  host to client   the race's settings          0xf4 bytes
 *   7  client to host   my car, control and name      0x1b bytes
 *   8  host to client   leave the menu for the race        4 bytes
 *   9  client to host   in the race: here                  4 bytes
 *  10  host to client   in the race: everyone is here      4 bytes
 *
 * They go as the original lays them out, little-endian, which every
 * host this port runs on is.
 */

#include <stdio.h>
#include <string.h>

#include "dplay.h"
#include "game.h"
#include "lisa/file.h"
#include "lisa/log.h"
#include "net.h"
#include "settings.h"
#include "structs/car.h"
#include "structs/car_phys.h"

extern s32 r_54f97c, r_54f9a8;          /* race/init.c: the race's message */

/* ------------------------------------------------------------ the session */

/* A player's name and DirectPlay's caps for it, 0x64 bytes. */
struct net_caps {
    s32 host;                           /* DPCAPS_ISHOST */
    s32 max_buffer;
    s32 hundred_baud;
    s32 latency;
    s32 timeout;
};

struct net_player {
    char name[0x50];                    /* +0x00 */
    struct net_caps caps;               /* +0x50 */
};

_Static_assert(offsetof(struct net_player, caps) == 0x50, "net_player.caps");
_Static_assert(sizeof(struct net_player) == 0x64, "net_player");

enum { NET_MAX = DP_MAX_PLAYERS };

s32 g_net_players;                      /* 0x50f7a8 */
static dpid s_player_id[NET_MAX];       /* 0x50f7b0 */
static s32 s_player_active[NET_MAX];    /* 0x50f878, 0 once gone */
static dpid s_enum_id[NET_MAX];         /* 0x50f8d8, EnumPlayers' */
static s32 s_enum_new[NET_MAX];         /* 0x50f988 */
static struct net_player s_enum[NET_MAX];       /* 0x50f9f0 */
static struct net_player s_players[NET_MAX];    /* 0x5102f0 */
static s32 s_enum_count;                /* 0x510bec */
static s32 s_full;                      /* 0x50f80c, 0x17 players */
static struct net_caps s_caps;          /* 0x50f860, this player's */
static dpid s_local_id;                 /* 0x50f874 */
static char s_player_name[0x50];        /* 0x50f810 */
static struct dp_session_desc s_desc;   /* 0x50f938 */
static s32 s_connected;                 /* 0x4bab00, 0 once the session is lost */
static s32 s_dp;                        /* 0x4bab04, the DirectPlay object */
static s32 s_dialog_busy;               /* 0x4bab08, searching */
static s32 s_4baaf8, s_4baafc;          /* cleared by net_connect, never read */

/* The dialog's controls (0x66): the service providers (0x3eb), the
 * sessions found (0x3ec), the Join button (0x3ef), and whether it has
 * ended. The original's player and session names come from edit boxes
 * (0x3ed, 0x3ee); here both are player 1's name. */
static s32 s_provider_count, s_provider_sel;
static u32 s_sessions[16];
static s32 s_session_count, s_session_sel;
static s32 s_join_enabled;
static s32 s_dialog_ended;

static int s_option_host;
static char s_option_address[256];

enum {
    WM_CLOSE = 0x0002,
    WM_INITDIALOG = 0x0110,
    WM_COMMAND = 0x0111,
    ID_CANCEL = 1002,                   /* 0x3ea */
    ID_JOIN = 1007,                     /* 0x3ef */
    ID_SEARCH = 1008,                   /* 0x3f0 */
    ID_HOST = 1009                      /* 0x3f1 */
};

void net_set_option(int host, const char *address)
{
    s_option_host = host;
    snprintf(s_option_address, sizeof(s_option_address), "%s", address != NULL ? address : "");
}

/* 0x459d30: run at the game's exit; does nothing. */
int net_shutdown(void)
{
    return 0;
}

/* 0x45a610: leaves the session the dialog made, if any. */
static int net_dialog_close(void)
{
    if (s_dp && (g_net_state == 3 || g_net_state == 4 || g_net_state == 1)) {
        dp_destroy_player(s_local_id);
        dp_close();
        dp_release();
        s_dp = 0;
    }
    g_net_state = 0;
    return 1;
}

/* 0x45a830: DirectPlayCreate for the chosen service provider, then
 * QueryInterface for IDirectPlay2A. */
static dp_result net_create(void)
{
    dp_result hr = dp_create();

    if (hr == DP_OK) {
        s_dp = 1;
    }
    return hr;
}

/* 0x45a680: WM_INITDIALOG. The original lists the IPX service
 * providers DirectPlayEnumerate gives (0x45a7a0 keeps those with
 * DPSPGUID_IPX) and picks the first; here the one is UDP. The Join
 * button waits for a search. */
static int net_dialog_init(void)
{
    s_provider_count = 1;
    s_provider_sel = 0;
    s_session_count = 0;
    s_session_sel = -1;
    s_join_enabled = 0;
    return 0;
}

/* 0x45ac40: EnumSessions' callback, a session into the list. */
static int net_session_found(const struct dp_session_desc *desc, u32 *timeout, u32 flags,
                             void *context)
{
    (void)timeout;
    (void)context;
    if (flags & DPESC_TIMEDOUT) {
        return 0;
    }
    if (s_session_count < (s32)(sizeof(s_sessions) / sizeof(s_sessions[0]))) {
        s_sessions[s_session_count++] = desc->instance;
    }
    return 1;
}

/* 0x45ab50: Search. A new DirectPlay object, and the sessions on the
 * network into the list. The original lets the service provider pick
 * the time to wait; here it is 1.5 seconds. */
static int net_dialog_search(void)
{
    if (s_provider_sel == -1 || s_provider_count == 0) {
        return 0;
    }
    if (net_create() != DP_OK) {
        return 0;
    }
    memset(&s_desc, 0, sizeof(s_desc));
    s_session_count = 0;
    dp_enum_sessions(s_option_address[0] ? s_option_address : NULL, 1500, net_session_found,
                     NULL, DPENUMSESSIONS_AVAILABLE);
    return 0;
}

/* 0x45a8a0: Join. Opens the chosen session and makes this player. */
static int net_dialog_join(void)
{
    struct dp_name name;
    dpid id;

    if (s_session_sel == -1 || s_session_sel >= s_session_count) {
        return 0;
    }
    memset(&s_desc, 0, sizeof(s_desc));
    s_desc.instance = s_sessions[s_session_sel];
    if (dp_open(&s_desc, DPOPEN_JOIN) & 0x80000000u) {
        return 0;
    }
    snprintf(s_player_name, sizeof(s_player_name), "%s",
             (const char *)g_settings + SET_PLAYER1_NAME);
    name.size = sizeof(name);
    name.flags = 0;
    name.short_name = s_player_name;
    name.long_name = NULL;
    if (!(dp_create_player(&id, &name) & 0x80000000u)) {
        s_local_id = id;
    }
    return 0;
}

/* 0x45a9d0: Host. A new DirectPlay object, a session of up to 0x17
 * players that keeps itself alive and moves to another host when its
 * own leaves (DirectPlay's; this port's session ends instead), and
 * this player in it. */
static int net_dialog_host(void)
{
    struct dp_name name;
    char session[200];
    dpid id;

    if (s_provider_sel == -1 || s_provider_count == 0) {
        return 0;
    }
    net_create();
    snprintf(session, sizeof(session), "%s", (const char *)g_settings + SET_PLAYER1_NAME);
    memset(&s_desc, 0, sizeof(s_desc));
    s_desc.flags = DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST;
    s_desc.max_players = 0x17;
    snprintf(s_desc.name, sizeof(s_desc.name), "%s", session);
    if (dp_open(&s_desc, DPOPEN_CREATE) & 0x80000000u) {
        return 0;
    }
    name.size = sizeof(name);
    name.flags = 0;
    name.short_name = session;
    name.long_name = NULL;
    if (!(dp_create_player(&id, &name) & 0x80000000u)) {
        s_local_id = id;
    }
    return 0;
}

/* 0x45a3c0: the dialog's procedure, for the messages it handles. The
 * buttons set g_net_state: 1 searching, 2 cancelled, 3 hosting, 4
 * joined. The original takes any notification of control 1002 (the
 * low word of wParam only) as Cancel. */
static int net_dialog(u32 msg, u32 id)
{
    if (msg == WM_CLOSE) {
        if (!s_dialog_busy) {
            /* 0x45a710 frees the lists' items here. */
            s_dialog_ended = 1;
        }
        return 0;
    }
    if (msg == WM_INITDIALOG) {
        net_dialog_init();
        return 0;
    }
    if (msg != WM_COMMAND || s_dialog_busy) {
        return 0;
    }
    switch (id & 0xffff) {
    case ID_CANCEL:
        if (g_net_state != 0) {
            net_dialog_close();
        }
        g_net_state = 2;
        s_dialog_ended = 1;
        break;
    case ID_JOIN:
        if (g_net_state == 1) {
            g_net_state = 4;
            net_dialog_join();
            s_dialog_ended = 1;
        }
        break;
    case ID_SEARCH:
        net_dialog_close();
        g_net_state = 1;
        s_dialog_busy = 1;
        net_dialog_search();
        s_dialog_busy = 0;
        s_join_enabled = s_session_count != -1 && s_session_count != 0;
        s_session_sel = s_session_count > 0 ? 0 : -1;
        break;
    case ID_HOST:
        if (g_net_state != 0) {
            net_dialog_close();
        }
        g_net_state = 3;
        net_dialog_host();
        s_dialog_ended = 1;
        break;
    }
    return 0;
}

/* The port's stand-in for the player at the dialog (DialogBoxParamA):
 * --host presses Host; otherwise Search, then Join if a session turned
 * up, or else Host (or Cancel, for --connect). A session that could
 * not be hosted or joined is left, so the menu goes back; the original
 * would wait on the network page. */
static void net_dialog_run(void)
{
    s_dialog_ended = 0;
    s_local_id = 0;
    net_dialog(WM_INITDIALOG, 0);
    if (s_option_host) {
        net_dialog(WM_COMMAND, ID_HOST);
    } else {
        net_dialog(WM_COMMAND, ID_SEARCH);
        if (s_join_enabled) {
            net_dialog(WM_COMMAND, ID_JOIN);
        } else {
            net_dialog(WM_COMMAND, s_option_address[0] ? ID_CANCEL : ID_HOST);
        }
    }
    if (!s_dialog_ended) {
        net_dialog(WM_CLOSE, 0);
    }
    if ((g_net_state == 3 || g_net_state == 4) && s_local_id == 0) {
        log_printf("Network: could not %s a session\n", g_net_state == 3 ? "host" : "join");
        net_dialog_close();
        g_net_state = 2;
    } else if (g_net_state == 3) {
        log_printf("Network: hosting a session, port %d\n", DP_PORT);
    } else if (g_net_state == 4) {
        log_printf("Network: joined a session\n");
    }
}

/* 0x459d40: NETWORK GAME. Forgets the players, then runs the dialog
 * that hosts or joins a session. */
int net_connect(void)
{
    int i;

    g_net_state = 0;
    s_4baaf8 = 0;
    s_4baafc = 0;
    g_net_players = 0;
    s_enum_count = 0;
    s_connected = 1;
    memset(s_player_active, 0, sizeof(s_player_active));
    memset(s_player_id, 0, sizeof(s_player_id));
    memset(s_enum_new, 0, sizeof(s_enum_new));
    memset(s_enum_id, 0, sizeof(s_enum_id));
    for (i = 0; i < NET_MAX; i++) {
        s_enum[i].name[0] = '\0';
        s_players[i].name[0] = '\0';
        memset(&s_enum[i].caps, 0, sizeof(s_enum[i].caps));
        memset(&s_players[i].caps, 0, sizeof(s_players[i].caps));
    }
    net_dialog_run();
    (void)s_4baaf8;
    (void)s_4baafc;
    return 1;
}

/* 0x459e10: leaves the session, if hosting or joining one. */
int net_stop(void)
{
    if (s_dp && (g_net_state == 3 || g_net_state == 4)) {
        dp_destroy_player(s_local_id);
        dp_close();
        dp_release();
        s_dp = 0;
    }
    g_net_state = 0;
    s_connected = 0;
    return 1;
}

/* 0x45acd0: EnumPlayers' callback, a player into s_enum. The original
 * copies the whole name; it is cut to the entry here. */
static int net_player_found(dpid id, u32 type, const struct dp_name *name, u32 flags,
                            void *context)
{
    (void)type;
    (void)flags;
    (void)context;
    if (s_enum_count == NET_MAX) {
        return 1;
    }
    s_enum_id[s_enum_count] = id;
    snprintf(s_enum[s_enum_count].name, sizeof(s_enum[0].name), "%s",
             name->short_name != NULL ? name->short_name : "");
    s_enum_new[s_enum_count] = 1;
    s_enum_count++;
    return 1;
}

static void caps_from(struct net_caps *c, const struct dp_caps *dc)
{
    c->host = (dc->flags & DPCAPS_ISHOST) ? 1 : 0;
    c->max_buffer = (s32)dc->max_buffer_size;
    c->hundred_baud = (s32)dc->hundred_baud;
    c->latency = (s32)dc->latency;
    c->timeout = (s32)dc->timeout;
}

/* 0x459e80
 * The other players, again: those gone are marked inactive, those new
 * are added at the end of the table, and each one's caps read. The
 * table only grows: a player keeps its index, and so its driver. A
 * player that left makes the caps fail, so this returns early from
 * then on, its work done. */
int net_poll_players(void)
{
    struct dp_caps caps;
    int i, j, n;

    s_enum_count = 0;
    memset(s_enum_new, 0, sizeof(s_enum_new));
    memset(s_enum_id, 0, sizeof(s_enum_id));
    for (i = 0; i < NET_MAX; i++) {
        s_enum[i].name[0] = '\0';
        memset(&s_enum[i].caps, 0, sizeof(s_enum[i].caps));
    }
    s_full = 0;
    if (dp_enum_players(net_player_found, NULL, DPENUMPLAYERS_REMOTE) != DP_OK) {
        return 0xbb8;
    }
    memset(&caps, 0, sizeof(caps));
    caps.size = sizeof(caps);
    if (dp_get_caps(&caps, 0) != DP_OK) {
        return 0xbc2;
    }
    caps_from(&s_caps, &caps);

    for (i = 0; i < g_net_players; i++) {
        if (s_player_active[i] == 1) {
            for (j = 0; j < s_enum_count && s_enum_id[j] != s_player_id[i]; j++) {
            }
            if (j == s_enum_count) {
                s_player_active[i] = 0;
            }
        }
    }
    if (g_net_players == NET_MAX) {
        s_full = 1;
        return 0xcb2;
    }
    for (j = 0; j < s_enum_count; j++) {
        if (s_enum_new[j] != 1) {
            continue;
        }
        for (n = 0; n < g_net_players && s_player_id[n] != s_enum_id[j]; n++) {
        }
        /* The original has no room check: DirectPlay's session holds
         * no more than the table. */
        if (n == g_net_players && n < NET_MAX) {
            s_player_id[n] = s_enum_id[j];
            s_player_active[n] = 1;
            s_players[n] = s_enum[j];
            g_net_players++;
        }
    }
    for (i = 0; i < g_net_players; i++) {
        memset(&caps, 0, sizeof(caps));
        caps.size = sizeof(caps);
        if (dp_get_player_caps(s_player_id[i], &caps, 0) != DP_OK) {
            return 0xbcc;
        }
        caps_from(&s_players[i].caps, &caps);
    }
    return 1;
}

/* 0x45a130: unused. */
int net_45a130(void)
{
    return 2;
}

/* 0x45a140
 * Sends a message to player `to` (0x1000000: everyone), not
 * guaranteed. Returns 1, or the game's code for DirectPlay's error. */
int net_send(s32 to, const void *buf, u32 size)
{
    dpid id;
    dp_result hr;

    if (to == 0x1000000) {
        id = DPID_ALLPLAYERS;
    } else if (to < 0 || to >= NET_MAX || s_player_active[to] != 1) {
        return 0xbd6;
    } else {
        id = s_player_id[to];
    }
    hr = dp_send(s_local_id, id, DPSEND_HIGHPRIORITY, buf, size);
    switch (hr) {
    case DPERR_BUSY:
        return 0xbe0;
    case DPERR_SENDTOOBIG:
        return 0xbea;
    case DPERR_INVALIDOBJECT:
        return 0xbeb;
    case DPERR_INVALIDPARAMS:
        return 0xbec;
    case DPERR_INVALIDPLAYER:
        return 0xbed;
    }
    return hr == DP_OK ? 1 : 0xbf4;
}

/* 0x45a2b0: a system message. Losing the session, or becoming its host
 * when the host left, ends the game's connection; other system
 * messages are skipped (1). */
static int net_system_message(const void *buf, const u32 *size, dp_result hr)
{
    u32 type;

    (void)size;
    if (hr == DPERR_BUFFERTOOSMALL) {
        return 0xc44;
    }
    if (hr == DPERR_INVALIDPARAMS) {
        return 0xc4e;
    }
    if (hr != DP_OK) {
        return 0xc58;
    }
    memcpy(&type, buf, sizeof(type));
    if (type == DPSYS_SESSIONLOST) {
        s_connected = 0;
        return 0xc8a;
    }
    if (type == DPSYS_HOST) {
        s_connected = 0;
        return 0xc80;
    }
    return 1;
}

/* 0x45a310: a player's message: *from becomes its sender's index. */
static int net_received(s32 *from, const void *buf, const u32 *size, dp_result hr, dpid id)
{
    int i;

    (void)buf;
    (void)size;
    switch (hr) {
    case DPERR_BUFFERTOOSMALL:
        return 0xc08;
    case DPERR_INVALIDPARAMS:
        return 0xc12;
    case DPERR_INVALIDPLAYER:
        return 0xc1c;
    case DPERR_NOMESSAGES:
        return 0xc26;
    }
    if (hr != DP_OK) {
        return 0xc30;
    }
    for (i = 0; i < g_net_players; i++) {
        if (s_player_id[i] == id && s_player_active[i] == 1) {
            break;
        }
    }
    /* Not found, the original reads the flag past the players, which is
     * 0 (or past the table, at 0x17). */
    if (i >= NET_MAX || s_player_active[i] == 0) {
        return 0xc1c;
    }
    if (i < g_net_players) {
        *from = i;
        return 1;
    }
    return 0xc31;
}

/* 0x45a1f0
 * Takes the next message for this player into buf (*size, in: its
 * size; out: the message's), skipping system messages. `any` 1 takes
 * one from anyone; otherwise *from must be a player, though DirectPlay
 * is asked only for messages to this player, so it still comes from
 * anyone. Returns 1 with *from the sender's index, or an error code,
 * also left in g_file_error. */
int net_receive(s32 *from, void *buf, u32 *size, int any)
{
    u32 flags;
    dpid id_from = 0xffffffffu;   /* uninitialised in the original */
    dp_result hr;
    int r;

    if (any == 1) {
        flags = DPRECEIVE_ALL;
    } else {
        flags = DPRECEIVE_TOPLAYER;
        if (*from < 0 || *from >= NET_MAX || s_player_active[*from] != 1) {
            return 0xbd7;
        }
        id_from = s_player_id[*from];
    }
    for (;;) {
        hr = dp_receive(&id_from, &s_local_id, flags, buf, size);
        if (id_from != DPID_SYSMSG) {
            break;
        }
        r = net_system_message(buf, size, hr);
        g_file_error = r;
        if (r != 1) {
            return r;
        }
    }
    r = net_received(from, buf, size, hr, id_from);
    g_file_error = r;
    return r;
}

/* ------------------------------------------------------------ the messages */

/* Kind 6, the host's settings, 0xf4 bytes. */
struct net_settings {
    s32 kind;                           /* +0x00 */
    s32 track;                          /* +0x04, settings 0x0f3 */
    s32 f08;                            /* +0x08, settings 0x00c */
    s32 f0c;                            /* +0x0c, settings 0x014 */
    s32 race_mode;                      /* +0x10, settings 0x008 */
    s32 drivers;                        /* +0x14, settings 0x153 */
    s32 players;                        /* +0x18 */
    s32 control[8];                     /* +0x1c, settings 0x15f */
    s32 car[8];                         /* +0x3c, settings 0x01c */
    s32 order[8];                       /* +0x5c, settings 0x133 */
    char name[8][15];                   /* +0x7c, settings 0x06c */
};

_Static_assert(offsetof(struct net_settings, control) == 0x1c, "net_settings.control");
_Static_assert(offsetof(struct net_settings, car) == 0x3c, "net_settings.car");
_Static_assert(offsetof(struct net_settings, order) == 0x5c, "net_settings.order");
_Static_assert(offsetof(struct net_settings, name) == 0x7c, "net_settings.name");
_Static_assert(sizeof(struct net_settings) == 0xf4, "net_settings");

/* Kind 7, a client's car, control and name, 0x1b bytes. */
#pragma pack(push, 1)
struct net_join {
    s32 kind;                           /* +0x00 */
    s32 car;                            /* +0x04 */
    s32 control;                        /* +0x08, 2 */
    char name[15];                      /* +0x0c */
};

/* Kind 5, a car, 0xc6 bytes. */
struct net_car {
    s32 kind;                           /* +0x00 */
    s16 car;                            /* +0x04, the sender's index */
    s16 event;                          /* +0x06, car.net_event */
    s16 message;                        /* +0x08, r_54f97c, 0 none */
    s16 message_car;                    /* +0x0a, r_54f9a8 */
    double y;                           /* +0x0c, car */
    double f108;                        /* +0x14 */
    double f110;                        /* +0x1c */
    double heading;                     /* +0x24, car_phys */
    double yaw_rate;                    /* +0x2c */
    double x;                           /* +0x34 */
    double z;                           /* +0x3c */
    double vx;                          /* +0x44 */
    double vz;                          /* +0x4c */
    double speed;                       /* +0x54 */
    double lateral;                     /* +0x5c */
    double steer;                       /* +0x64 */
    double traction;                    /* +0x6c */
    double drive;                       /* +0x74 */
    double impact;                      /* +0x7c */
    double contact_x;                   /* +0x84 */
    double contact_z;                   /* +0x8c */
    double slip;                        /* +0x94, car */
    double front_slip;                  /* +0x9c */
    double f3a4;                        /* +0xa4 */
    s32 throttle_on;                    /* +0xac */
    double f0b8;                        /* +0xb0 */
    double f0c0;                        /* +0xb8 */
    s8 brake;                           /* +0xc0, car_phys */
    s8 f53c;                            /* +0xc1, car */
    s8 f3a0;                            /* +0xc2 */
    s8 f528;                            /* +0xc3 */
    s8 f560;                            /* +0xc4 */
    s8 slipping;                        /* +0xc5, car_phys */
};
#pragma pack(pop)

_Static_assert(sizeof(struct net_join) == 0x1b, "net_join");
_Static_assert(offsetof(struct net_car, y) == 0x0c, "net_car.y");
_Static_assert(offsetof(struct net_car, heading) == 0x24, "net_car.heading");
_Static_assert(offsetof(struct net_car, slip) == 0x94, "net_car.slip");
_Static_assert(offsetof(struct net_car, throttle_on) == 0xac, "net_car.throttle_on");
_Static_assert(offsetof(struct net_car, brake) == 0xc0, "net_car.brake");
_Static_assert(sizeof(struct net_car) == 0xc6, "net_car");

/* The receive buffers, 0x400 bytes each, as s32 for the kind. */
static s32 s_menu_msg[0x100];           /* 0x4bfb50 */
static s32 s_settings_msg[0x100];       /* 0x4c0b50 */
static s32 s_ready_msg[0x100];          /* 0x4c1b50 */
static s32 s_start_msg[0x100];          /* 0x4c2b50 */
static s32 s_car_msg[0x100];            /* 0x4c3b50 */
static struct net_car s_car_out;        /* 0x63a310 */

static s32 s_drivers;                   /* 0x63a3d8, the race's drivers */
static s32 s_host_index = -1;           /* 0x63a3dc, a client's host */
static s32 s_menu_ticks;                /* 0x63a458 */
static s32 s_my_slot;                   /* 0x63a45c, a client's driver */
static s32 s_race_ticks;                /* 0x63a464 */
static s32 s_answered[NET_MAX];         /* 0x63a470, by player */

static s32 rd32(const u8 *p)
{
    s32 v;

    memcpy(&v, p, sizeof(v));
    return v;
}

static void wr32(u8 *p, s32 v)
{
    memcpy(p, &v, sizeof(v));
}

/* sprintf "%s" of a name of the settings' 15 byte fields. */
static void copy_name(char *dst, const char *src)
{
    snprintf(dst, 15, "%s", src);
}

/* 0x40ce90 */
void net_reset(struct net_game *game)
{
    (void)game;
    memset(s_answered, 0, sizeof(s_answered));
    s_menu_ticks = 0;
    s_race_ticks = 0;
    s_host_index = -1;
    s_my_slot = 0;
}

void net_clear_answers(void)
{
    memset(s_answered, 0, sizeof(s_answered));
}

/* 0x40cec0
 * The network page, every sixth frame. The host takes in new players,
 * tells each its driver (kind 1) and sends them all the settings
 * (kind 6), then reads one message: a client's car, control and name
 * (kind 7). Returns 1 when the number of players changed. A client
 * reads a message for each player; kind 1 from the host makes it
 * joined (stage 1), and it answers the host with kind 7 each time. */
int net_menu(struct net_game *game, u8 *settings)
{
    int changed = 0;
    s32 from;
    u32 size;
    int i, n;

    if (game->mode == 3) {
        struct net_settings msg;

        if (++s_menu_ticks <= 5) {
            return changed;
        }
        s_menu_ticks = 0;
        net_poll_players();
        for (i = 0, n = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 1) {
                n++;
            }
        }
        if (game->players != n) {
            changed = 1;
        }
        game->players = n;
        for (i = 0; i < g_net_players; i++) {
            s_menu_msg[0] = 1;
            s_menu_msg[1] = i + 1;
            net_send(i, s_menu_msg, 8);
        }
        memset(&msg, 0, sizeof(msg));
        msg.kind = 6;
        msg.drivers = rd32(settings + 0x153);
        s_drivers = msg.drivers;
        msg.track = rd32(settings + 0x0f3);
        msg.players = game->players;
        msg.f08 = rd32(settings + 0x00c);
        msg.f0c = rd32(settings + 0x014);
        msg.race_mode = rd32(settings + 0x008);
        for (i = 0; i < 8; i++) {
            msg.control[i] = rd32(settings + 0x15f + 4 * i);
            msg.car[i] = rd32(settings + 0x01c + 4 * i);
            msg.order[i] = rd32(settings + 0x133 + 4 * i);
            copy_name(msg.name[i], (const char *)settings + 0x06c + 15 * i);
        }
        for (i = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 1) {
                net_send(i, &msg, sizeof(msg));
            }
        }
        size = 0x400;
        i = net_receive(&from, s_menu_msg, &size, 1);
        wr32(settings + 0x15f, 2);
        if (i == 1 && s_menu_msg[0] == 7) {
            const struct net_join *join = (const struct net_join *)s_menu_msg;

            wr32(settings + 0x020 + 4 * from, join->car);
            wr32(settings + 0x163 + 4 * from, join->control);
            copy_name((char *)settings + 0x07b + 15 * from, join->name);
        }
        return changed;
    }
    if (game->mode == 4) {
        s_menu_ticks++;
        net_poll_players();
        s_my_slot = 0;
        if (s_menu_ticks > 5) {
            s_menu_ticks = 0;
            for (i = 0; i < g_net_players; i++) {
                struct net_join join;

                from = i;
                size = 0x400;
                if (net_receive(&from, s_menu_msg, &size, 1) == 1 && s_menu_msg[0] == 1
                    && s_host_index == -1) {
                    s_host_index = from;
                    s_my_slot = s_menu_msg[1];
                    game->stage = 1;
                }
                join.kind = 7;
                join.car = rd32(settings + 0x01c);
                join.control = 2;
                copy_name(join.name, (const char *)settings + 0x06c);
                net_send(s_host_index, &join, sizeof(join));
            }
        }
        return 0;
    }
    return game->mode;
}

/* 0x40d200
 * Leaving the menu for the race. The host reads a message (and drops
 * it), tells everyone to go (kind 8) and goes itself (stage 2). A
 * client takes the host's settings (kind 6), with its own driver and
 * driver 0 swapped, until the host says go. */
int net_send_settings(struct net_game *game, u8 *settings)
{
    s32 from;
    u32 size;
    int i;

    if (game->mode == 3) {
        size = 0x400;
        net_receive(&from, s_settings_msg, &size, 1);
        s_settings_msg[0] = 8;
        size = 4;
        for (i = 0; i < g_net_players; i++) {
            net_send(i, s_settings_msg, size);
        }
        game->stage = 2;
    }
    if (game->mode == 4) {
        net_poll_players();
        from = s_host_index;
        size = 0x400;
        if (net_receive(&from, s_settings_msg, &size, 0) == 1) {
            const struct net_settings *msg = (const struct net_settings *)s_settings_msg;

            if (msg->kind == 6) {
                s_drivers = msg->drivers;
                game->players = msg->players;
                for (i = 0; i < s_drivers; i++) {
                    if (net_slot(i) != 0) {
                        wr32(settings + 0x15f + 4 * net_slot(i), msg->control[i]);
                        wr32(settings + 0x01c + 4 * net_slot(i), msg->car[i]);
                        copy_name((char *)settings + 0x06c + 15 * net_slot(i), msg->name[i]);
                    }
                    /* Only 8 in the message; the original reads past it. */
                    wr32(settings + 0x133 + 4 * net_slot(i), i < 8 ? msg->order[i] : 0);
                }
                wr32(settings + 0x153, s_drivers);
                wr32(settings + 0x008, msg->race_mode);
                wr32(settings + 0x0f3, msg->track);
                wr32(settings + 0x00c, msg->f08);
                wr32(settings + 0x014, msg->f0c);
            }
            if (s_settings_msg[0] == 8) {
                game->stage = 2;
            }
        }
    }
    return game->mode;
}

/* 0x40d3d0
 * The race, stage 2: every player loaded. Each client says so (kind
 * 9) every sixth frame; once all have, the host says so (kind 10) and
 * both go on to stage 3. */
void net_wait_ready(struct net_game *game)
{
    s32 from;
    u32 size;
    int i, n;

    if (game->mode == 3) {
        s_ready_msg[0] = 0;
        for (i = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 0) {
                continue;
            }
            from = i;
            size = 0x400;
            while (net_receive(&from, s_ready_msg, &size, 0) == 1) {
                if (s_ready_msg[0] == 9) {
                    s_answered[from] = 1;
                }
                s_ready_msg[0] = 0;
                from = i;
                size = 0x400;
            }
        }
        for (i = 0, n = 0; i < g_net_players; i++) {
            if (s_answered[i] == 1) {
                n++;
            }
        }
        if (g_net_players != n) {
            return;
        }
        s_ready_msg[0] = 10;
        for (i = 0; i < g_net_players; i++) {
            net_send(i, s_ready_msg, 4);
        }
        game->stage = 3;
        if (g_net_players > 0) {
            memset(s_answered, 0, (size_t)g_net_players * sizeof(s_answered[0]));
        }
        return;
    }
    if (game->mode == 4) {
        if (++s_race_ticks > 5) {
            s_race_ticks = 0;
            s_ready_msg[0] = 9;
            net_send(s_host_index, s_ready_msg, 4);
        }
        from = s_host_index;
        size = 0x400;
        if (net_receive(&from, s_ready_msg, &size, 0) == 1 && s_ready_msg[0] == 10
            && from == s_host_index) {
            game->stage = 3;
        }
    }
}

/* 0x40d5c0
 * The race, stage 3: the same again with kinds 3 and 4, every eleventh
 * frame, and both start racing (stage 4). The host sends its go once. */
void net_wait_start(struct net_game *game)
{
    s32 from;
    u32 size;
    int i, n;

    if (game->mode == 3) {
        s_start_msg[0] = 0;
        for (i = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 0) {
                continue;
            }
            from = i;
            size = 0x400;
            while (net_receive(&from, s_start_msg, &size, 0) == 1) {
                if (s_start_msg[0] == 3) {
                    s_answered[from] = 1;
                }
                s_start_msg[0] = 0;
                from = i;
                size = 0x400;
            }
        }
        for (i = 0, n = 0; i < g_net_players; i++) {
            if (s_answered[i] == 1) {
                n++;
            }
        }
        if (g_net_players != n) {
            return;
        }
        s_start_msg[0] = 4;
        for (i = 0; i < g_net_players; i++) {
            net_send(i, s_start_msg, 4);
        }
        game->stage = 4;
        return;
    }
    if (game->mode == 4) {
        if (++s_race_ticks > 10) {
            s_race_ticks = 0;
            s_start_msg[0] = 3;
            net_send(s_host_index, s_start_msg, 4);
        }
        from = s_host_index;
        size = 0x400;
        if (net_receive(&from, s_start_msg, &size, 0) == 1 && s_start_msg[0] == 4
            && from == s_host_index) {
            game->stage = 4;
        }
    }
}

/* A car from a kind 5 message into car `k`, at the race's `clock`
 * (net_receive_cars, the same for host and client). */
static void read_car(const struct net_car *m, struct car *c, struct car_phys *p, double clock)
{
    c->y = m->y;
    c->f108 = m->f108;
    c->f110 = m->f110;
    if (m->event != 0) {
        c->net_event = m->event;
    }
    if (m->message != 0) {
        r_54f97c = m->message;
        r_54f9a8 = (s16)net_slot(m->message_car);
    }
    p->heading = m->heading;
    p->yaw_rate = m->yaw_rate;
    p->f038[1] = p->f038[0];
    p->f050[1] = p->f050[0];
    p->f038[0] = m->x;
    p->f050[0] = m->z;
    p->x = m->x;
    p->z = m->z;
    p->vx = m->vx;
    p->vz = m->vz;
    p->speed = m->speed;
    p->lateral = m->lateral;
    p->slipping = m->slipping;
    p->steer = m->steer;
    p->traction = m->traction;
    p->brake = m->brake;
    p->drive = m->drive;
    p->impact = m->impact;
    p->contact_x = m->contact_x;
    p->contact_z = m->contact_z;
    c->front_slip = m->front_slip;
    c->slip = m->slip;
    c->f3a4 = m->f3a4;
    c->f3a0 = m->f3a0;
    c->f528 = m->f528;
    c->throttle_on = m->throttle_on;
    c->f0b8 = m->f0b8;
    c->f0c0 = m->f0c0;
    c->f560 = m->f560;
    /* The last two packets' clocks. */
    c->f61c = c->clock;
    c->f62c = c->f624;
    c->f624 = clock;
    if (m->f53c != c->f53c) {
        if (m->f53c != 0) {
            c->f550 = 1;
        }
        c->f53c = m->f53c;
    } else {
        c->f550 = 0;
    }
    c->net_fresh = 1;
}

/* 0x40d7a0
 * The race: the cars from the network. The host takes every client's
 * car; a client takes every car from its host but its own. */
void net_receive_cars(struct net_game *game, struct car *cars, struct car_phys *phys,
                      double clock)
{
    const struct net_car *m = (const struct net_car *)s_car_msg;
    s32 from;
    u32 size;
    int i, k;

    if (game->mode == 3) {
        s_car_msg[0] = 0;
        for (i = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 0) {
                continue;
            }
            from = i;
            size = 0x400;
            while (net_receive(&from, s_car_msg, &size, 0) == 1) {
                k = m->car;
                if (m->kind == 5 && k >= 0 && k < s_drivers) {
                    read_car(m, &cars[k], &phys[k], clock);
                }
                from = i;
                size = 0x400;
            }
        }
        return;
    }
    if (game->mode == 4) {
        from = s_host_index;
        s_car_msg[0] = 0;
        size = 0x400;
        if (net_receive(&from, s_car_msg, &size, 0) != 1) {
            return;
        }
        while (from == s_host_index) {
            if (m->kind == 5) {
                k = net_slot(m->car);
                if (k > 0 && k < s_drivers) {
                    read_car(m, &cars[k], &phys[k], clock);
                }
            }
            from = s_host_index;
            size = 0x400;
            if (net_receive(&from, s_car_msg, &size, 0) != 1) {
                return;
            }
        }
    }
}

/* A car into the kind 5 message (net_send_cars). The message's text
 * and its car go only when there is one; otherwise the last car stays
 * in the packet. */
static void write_car(s16 index, const struct car *c, const struct car_phys *p)
{
    struct net_car *m = &s_car_out;

    m->kind = 5;
    m->car = index;
    m->y = c->y;
    m->f108 = c->f108;
    m->f110 = c->f110;
    m->event = (s16)c->net_event;
    if (r_54f97c != 0) {
        m->message = (s16)r_54f97c;
        m->message_car = (s16)net_slot(r_54f9a8);
    } else {
        m->message = 0;
    }
    m->heading = p->heading;
    m->yaw_rate = p->yaw_rate;
    m->x = p->x;
    m->z = p->z;
    m->vx = p->vx;
    m->vz = p->vz;
    m->speed = p->speed;
    m->lateral = p->lateral;
    m->slipping = (s8)p->slipping;
    m->steer = p->steer;
    m->traction = p->traction;
    m->brake = (s8)p->brake;
    m->drive = p->drive;
    m->impact = p->impact;
    m->contact_x = p->contact_x;
    m->contact_z = p->contact_z;
    m->slip = c->slip;
    m->front_slip = c->front_slip;
    m->f3a4 = c->f3a4;
    m->f3a0 = (s8)c->f3a0;
    m->f528 = (s8)c->f528;
    m->throttle_on = c->throttle_on;
    m->f0b8 = c->f0b8;
    m->f0c0 = c->f0c0;
    m->f560 = (s8)c->f560;
    m->f53c = (s8)c->f53c;
}

/* 0x40e0a0
 * The race: the cars to the network, every frame. The host sends each
 * player every car; a client sends its host its own, as its driver.
 * Then the race's message is sent, and the cars' events. */
void net_send_cars(struct net_game *game, struct car *cars, struct car_phys *phys)
{
    int i, k;

    if (game->mode == 3) {
        for (i = 0; i < g_net_players; i++) {
            if (s_player_active[i] == 0) {
                continue;
            }
            for (k = 0; k < s_drivers; k++) {
                write_car((s16)k, &cars[k], &phys[k]);
                net_send(i, &s_car_out, sizeof(s_car_out));
            }
        }
        r_54f97c = 0;
    } else if (game->mode == 4) {
        write_car((s16)s_my_slot, &cars[0], &phys[0]);
        net_send(s_host_index, &s_car_out, sizeof(s_car_out));
        r_54f97c = 0;
    }
    for (k = 0; k < s_drivers; k++) {
        cars[k].net_event = 0;
    }
}

/* 0x40e690: a driver between the host's numbering and a client's: the
 * client's own driver and driver 0 trade places. */
s32 net_slot(s32 i)
{
    if (i == 0) {
        return s_my_slot;
    }
    return s_my_slot == i ? 0 : i;
}
