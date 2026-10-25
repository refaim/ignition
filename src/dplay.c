/*
 * dplay.c - a stand-in for the DirectPlay 3 that IGN_WIN.EXE races
 * over, on UDP. See dplay.h, and docs/network.md for the protocol.
 *
 * The host of a session is its name server, as in DirectPlay: it gives
 * each player an id, and sends everyone the table of players and their
 * addresses twice a second. Messages go straight from player to player.
 * A player not heard from for 15 seconds is dropped; a client that has
 * not heard from the host for that long has lost the session. Nothing
 * is resent: the game sends the same messages again until it gets an
 * answer, as it did over IPX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dplay.h"
#include "platform/platform.h"

enum {
    PKT_HEADER = 20,
    PKT_VERSION = 1,
    PKT_MAX = PKT_HEADER + 4 + DP_MAX_PLAYERS * 44 + 16,
    QUEUE = 256,
    FOUND_MAX = 16,
    BEAT_MS = 500,                      /* the table, and clients' keep alive */
    TIMEOUT_MS = 15000,
    JOIN_MS = 5000,
    JOIN_RETRY_MS = 250
};

/* A packet's kind, at +4. */
enum {
    PKT_ENUM = 1,       /* client to everyone: any sessions? */
    PKT_SESSION,        /* host: the session's description */
    PKT_JOIN,           /* client to host: a player, named; from is a nonce */
    PKT_WELCOME,        /* host: the player's id in `to`, the nonce in `from` */
    PKT_PLAYERS,        /* host to everyone: the table of players */
    PKT_DATA,           /* a game message from `from` to `to` (0: everyone) */
    PKT_KEEPALIVE,      /* client to host */
    PKT_LEAVE           /* a player leaves; from the host, the session ends */
};

struct player {
    dpid id;
    char name[DP_NAME_SIZE];
    struct plat_net_addr addr;
    u32 heard;                          /* plat_time_ms of the last packet */
    int host;
    int local;
    int seen;
};

struct message {
    dpid from, to;
    u32 size;
    u8 data[DP_MAX_MESSAGE];
};

static int s_created;                   /* dp_create */
static int s_open;                      /* a session is open */
static int s_hosting;
static int s_lost;
static int s_enumerating;
static u32 s_instance;
static struct dp_session_desc s_desc;
static struct plat_net_addr s_host;     /* a client's host */
static u32 s_host_heard, s_last_beat;
static dpid s_next_id, s_local_id;
static u32 s_nonce;
static dpid s_welcome_id;

static struct player s_players[DP_MAX_PLAYERS];
static int s_count;

static struct {
    struct dp_session_desc desc;
    struct plat_net_addr addr;
} s_found[FOUND_MAX];
static int s_found_count;

static struct message s_queue[QUEUE];
static unsigned s_head, s_tail;

static u32 random_u32(void)
{
    static u32 s_seed;

    if (s_seed == 0) {
        s_seed = (u32)time(NULL) ^ plat_time_ms() * 2654435761u;
    }
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 8) | 1;
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
    return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static int same_addr(const struct plat_net_addr *a, const struct plat_net_addr *b)
{
    return a->ip == b->ip && a->port == b->port;
}

/* Sends a packet of `kind` with `size` bytes of payload. */
static void send_packet(const struct plat_net_addr *to, int kind, dpid from, dpid dest,
                        const void *payload, u32 size)
{
    u8 buf[PKT_HEADER + DP_MAX_MESSAGE + 64];

    if (size > sizeof(buf) - PKT_HEADER) {
        return;
    }
    memcpy(buf, "IGNP", 4);
    buf[4] = (u8)kind;
    buf[5] = PKT_VERSION;
    buf[6] = (u8)size;
    buf[7] = (u8)(size >> 8);
    put32(buf + 8, s_instance);
    put32(buf + 12, from);
    put32(buf + 16, dest);
    if (size > 0) {
        memcpy(buf + PKT_HEADER, payload, size);
    }
    plat_net_send(to, buf, (int)(PKT_HEADER + size));
}

/* ------------------------------------------------------------ the queue */

static void queue_message(dpid from, dpid to, const void *data, u32 size)
{
    struct message *m;

    if (size > DP_MAX_MESSAGE) {
        return;
    }
    /* Full: the oldest goes, as nothing the game sends is guaranteed. */
    if (s_head - s_tail >= QUEUE) {
        s_tail++;
    }
    m = &s_queue[s_head % QUEUE];
    m->from = from;
    m->to = to;
    m->size = size;
    memcpy(m->data, data, size);
    s_head++;
}

/* A system message: dwType, then for players dwPlayerType and the id,
 * the start of DPMSG_CREATEPLAYERORGROUP and its kin. */
static void queue_system(u32 type, dpid id)
{
    u8 msg[12];

    put32(msg, type);
    put32(msg + 4, DPPLAYERTYPE_PLAYER);
    put32(msg + 8, id);
    queue_message(DPID_SYSMSG, s_local_id, msg, type == DPSYS_SESSIONLOST ? 4 : 12);
}

/* ------------------------------------------------------------ players */

static struct player *find_player(dpid id)
{
    int i;

    for (i = 0; i < s_count; i++) {
        if (s_players[i].id == id) {
            return &s_players[i];
        }
    }
    return NULL;
}

static struct player *find_addr(const struct plat_net_addr *addr)
{
    int i;

    for (i = 0; i < s_count; i++) {
        if (!s_players[i].local && same_addr(&s_players[i].addr, addr)) {
            return &s_players[i];
        }
    }
    return NULL;
}

static struct player *add_player(dpid id, const char *name, const struct plat_net_addr *addr)
{
    struct player *p;

    if (s_count >= DP_MAX_PLAYERS) {
        return NULL;
    }
    p = &s_players[s_count++];
    memset(p, 0, sizeof(*p));
    p->id = id;
    snprintf(p->name, sizeof(p->name), "%s", name);
    if (addr != NULL) {
        p->addr = *addr;
    }
    p->heard = plat_time_ms();
    return p;
}

static void remove_player(struct player *p)
{
    int i = (int)(p - s_players);

    memmove(&s_players[i], &s_players[i + 1], (size_t)(s_count - i - 1) * sizeof(*p));
    s_count--;
}

/* The host's table to every client: id, address (0 for the host's
 * own, which each client knows), host flag and name, 44 bytes each. */
static void send_players(void)
{
    u8 buf[4 + DP_MAX_PLAYERS * 44];
    u8 *p = buf + 4;
    int i;

    put32(buf, (u32)s_count);
    for (i = 0; i < s_count; i++, p += 44) {
        const struct player *pl = &s_players[i];

        put32(p, pl->id);
        put32(p + 4, pl->local ? 0 : pl->addr.ip);
        p[8] = (u8)(pl->local ? 0 : pl->addr.port);
        p[9] = (u8)((pl->local ? 0 : pl->addr.port) >> 8);
        p[10] = (u8)pl->host;
        p[11] = 0;
        memcpy(p + 12, pl->name, DP_NAME_SIZE);
    }
    for (i = 0; i < s_count; i++) {
        if (!s_players[i].local) {
            send_packet(&s_players[i].addr, PKT_PLAYERS, s_local_id, s_players[i].id, buf,
                        (u32)(p - buf));
        }
    }
}

/* A client's copy of the host's table: players that came are made,
 * players gone are destroyed, with their system messages. */
static void read_players(const u8 *p, u32 size)
{
    u32 n = size >= 4 ? get32(p) : 0;
    u32 i;
    int k;

    if (n > DP_MAX_PLAYERS || size < 4 + n * 44) {
        return;
    }
    for (k = 0; k < s_count; k++) {
        s_players[k].seen = s_players[k].local;
    }
    for (i = 0, p += 4; i < n; i++, p += 44) {
        dpid id = get32(p);
        struct plat_net_addr addr;
        struct player *pl;
        char name[DP_NAME_SIZE];

        addr.ip = get32(p + 4);
        addr.port = (u16)(p[8] | p[9] << 8);
        if (p[10] && addr.ip == 0) {
            addr = s_host;
        }
        memcpy(name, p + 12, DP_NAME_SIZE);
        name[DP_NAME_SIZE - 1] = '\0';
        pl = find_player(id);
        if (pl == NULL) {
            pl = add_player(id, name, &addr);
            if (pl == NULL) {
                continue;
            }
            queue_system(DPSYS_CREATEPLAYERORGROUP, id);
        }
        if (!pl->local) {
            pl->addr = addr;
            pl->host = p[10];
        }
        pl->seen = 1;
    }
    for (k = s_count - 1; k >= 0; k--) {
        if (!s_players[k].seen) {
            queue_system(DPSYS_DESTROYPLAYERORGROUP, s_players[k].id);
            remove_player(&s_players[k]);
        }
    }
}

/* The host is gone: the players with it. */
static void lose_session(void)
{
    int k;

    if (s_lost) {
        return;
    }
    s_lost = 1;
    for (k = s_count - 1; k >= 0; k--) {
        if (!s_players[k].local) {
            remove_player(&s_players[k]);
        }
    }
    queue_system(DPSYS_SESSIONLOST, 0);
}

/* ------------------------------------------------------------ the socket */

static void handle(const struct plat_net_addr *from, const u8 *buf, int n)
{
    u32 size, session;
    dpid src, dest;
    const u8 *payload = buf + PKT_HEADER;
    struct player *pl;
    u32 now = plat_time_ms();

    if (n < PKT_HEADER || memcmp(buf, "IGNP", 4) != 0 || buf[5] != PKT_VERSION) {
        return;
    }
    size = buf[6] | (u32)buf[7] << 8;
    if (size > (u32)n - PKT_HEADER) {
        return;
    }
    session = get32(buf + 8);
    src = get32(buf + 12);
    dest = get32(buf + 16);

    if (buf[4] == PKT_ENUM) {
        if (s_open && s_hosting) {
            u8 desc[16 + DP_NAME_SIZE];

            put32(desc, s_desc.flags);
            put32(desc + 4, s_desc.max_players);
            put32(desc + 8, (u32)s_count);
            put32(desc + 12, 0);
            memcpy(desc + 16, s_desc.name, DP_NAME_SIZE);
            send_packet(from, PKT_SESSION, 0, 0, desc, sizeof(desc));
        }
        return;
    }
    if (buf[4] == PKT_SESSION) {
        int i;

        if (!s_enumerating || size < 16 + DP_NAME_SIZE || s_found_count >= FOUND_MAX) {
            return;
        }
        for (i = 0; i < s_found_count; i++) {
            if (s_found[i].desc.instance == session) {
                return;
            }
        }
        s_found[i].addr = *from;
        s_found[i].desc.flags = get32(payload);
        s_found[i].desc.instance = session;
        s_found[i].desc.max_players = get32(payload + 4);
        s_found[i].desc.current_players = get32(payload + 8);
        memcpy(s_found[i].desc.name, payload + 16, DP_NAME_SIZE);
        s_found[i].desc.name[DP_NAME_SIZE - 1] = '\0';
        s_found_count++;
        return;
    }
    if (!s_open || session != s_instance) {
        return;
    }
    if (s_hosting) {
        pl = find_addr(from);
        if (pl != NULL) {
            pl->heard = now;
        }
        if (buf[4] == PKT_JOIN && size >= DP_NAME_SIZE) {
            char name[DP_NAME_SIZE];

            if (pl == NULL) {
                if (s_count >= (int)s_desc.max_players) {
                    return;
                }
                memcpy(name, payload, DP_NAME_SIZE);
                name[DP_NAME_SIZE - 1] = '\0';
                pl = add_player(s_next_id++, name, from);
                if (pl == NULL) {
                    return;
                }
                queue_system(DPSYS_CREATEPLAYERORGROUP, pl->id);
            }
            /* A WELCOME lost on the way: the JOIN comes again. */
            send_packet(from, PKT_WELCOME, src, pl->id, NULL, 0);
            send_players();
            return;
        }
        if (pl == NULL) {
            return;
        }
        if (buf[4] == PKT_LEAVE) {
            queue_system(DPSYS_DESTROYPLAYERORGROUP, pl->id);
            remove_player(pl);
            send_players();
            return;
        }
    } else {
        if (same_addr(from, &s_host)) {
            s_host_heard = now;
        }
        if (buf[4] == PKT_WELCOME) {
            if (src == s_nonce) {
                s_welcome_id = dest;
            }
            return;
        }
        if (buf[4] == PKT_PLAYERS && same_addr(from, &s_host)) {
            if (s_local_id != 0) {
                read_players(payload, size);
            }
            return;
        }
        if (buf[4] == PKT_LEAVE && same_addr(from, &s_host)) {
            lose_session();
            return;
        }
    }
    if (buf[4] == PKT_DATA && s_local_id != 0 && (dest == DPID_ALLPLAYERS || dest == s_local_id)
        && find_player(src) != NULL) {
        queue_message(src, s_local_id, payload, size);
    }
}

/* Reads every waiting datagram, then keeps the session alive. */
void dp_pump(void)
{
    u8 buf[PKT_MAX + DP_MAX_MESSAGE];
    struct plat_net_addr from;
    u32 now;
    int n, k;

    while ((n = plat_net_recv(&from, buf, sizeof(buf))) > 0) {
        handle(&from, buf, n);
    }
    if (!s_open || s_lost) {
        return;
    }
    now = plat_time_ms();
    if (s_hosting) {
        int dropped = 0;

        for (k = s_count - 1; k >= 0; k--) {
            if (!s_players[k].local && now - s_players[k].heard > TIMEOUT_MS) {
                queue_system(DPSYS_DESTROYPLAYERORGROUP, s_players[k].id);
                remove_player(&s_players[k]);
                dropped = 1;
            }
        }
        if (dropped || now - s_last_beat >= BEAT_MS) {
            s_last_beat = now;
            send_players();
        }
    } else if (s_local_id != 0) {
        if (now - s_host_heard > TIMEOUT_MS) {
            lose_session();
        } else if (now - s_last_beat >= BEAT_MS) {
            s_last_beat = now;
            send_packet(&s_host, PKT_KEEPALIVE, s_local_id, 0, NULL, 0);
        }
    }
}

/* ------------------------------------------------------------ the methods */

dp_result dp_create(void)
{
    if (!plat_net_open(PLAT_NET_ANY)) {
        return DPERR_UNAVAILABLE;
    }
    s_created = 1;
    return DP_OK;
}

void dp_release(void)
{
    dp_close();
    plat_net_close();
    s_created = 0;
}

/* Parses "host" or "host:port". */
static int parse_address(const char *text, struct plat_net_addr *addr)
{
    char name[256];
    const char *colon = strrchr(text, ':');
    size_t len = colon != NULL ? (size_t)(colon - text) : strlen(text);

    if (len >= sizeof(name)) {
        return 0;
    }
    memcpy(name, text, len);
    name[len] = '\0';
    addr->port = colon != NULL ? (u16)strtoul(colon + 1, NULL, 10) : DP_PORT;
    return plat_net_resolve(name, &addr->ip);
}

dp_result dp_enum_sessions(const char *address, u32 timeout, dp_enum_sessions_fn fn,
                           void *context, u32 flags)
{
    struct plat_net_addr to[3];
    int targets = 0, i;
    u32 start, last = 0;

    (void)flags;
    if (!s_created) {
        return DPERR_INVALIDOBJECT;
    }
    to[targets].ip = PLAT_NET_BROADCAST;
    to[targets++].port = DP_PORT;
    to[targets].ip = PLAT_NET_LOOPBACK;
    to[targets++].port = DP_PORT;
    if (address != NULL && parse_address(address, &to[targets])) {
        targets++;
    }
    s_found_count = 0;
    s_enumerating = 1;
    start = plat_time_ms();
    for (;;) {
        u32 now = plat_time_ms();

        if (now - start >= timeout) {
            break;
        }
        if (last == 0 || now - last >= 300) {
            last = now | 1;
            for (i = 0; i < targets; i++) {
                send_packet(&to[i], PKT_ENUM, 0, 0, NULL, 0);
            }
        }
        plat_net_wait(50);
        dp_pump();
    }
    s_enumerating = 0;
    for (i = 0; i < s_found_count; i++) {
        if (!fn(&s_found[i].desc, &timeout, 0, context)) {
            return DP_OK;
        }
    }
    fn(NULL, &timeout, DPESC_TIMEDOUT, context);
    return DP_OK;
}

dp_result dp_open(const struct dp_session_desc *desc, u32 flags)
{
    int i;

    if (!s_created) {
        return DPERR_INVALIDOBJECT;
    }
    s_count = 0;
    s_tail = s_head;
    s_lost = 0;
    s_local_id = 0;
    s_last_beat = 0;
    if (flags & DPOPEN_CREATE) {
        if (!plat_net_open(DP_PORT)) {
            plat_net_open(PLAT_NET_ANY);
            return DPERR_CANTCREATESESSION;
        }
        s_desc = *desc;
        if (s_desc.max_players == 0 || s_desc.max_players > DP_MAX_PLAYERS) {
            s_desc.max_players = DP_MAX_PLAYERS;
        }
        s_instance = random_u32();
        s_desc.instance = s_instance;
        s_hosting = 1;
        s_next_id = 1;
        s_open = 1;
        return DP_OK;
    }
    for (i = 0; i < s_found_count; i++) {
        if (s_found[i].desc.instance == desc->instance) {
            s_desc = s_found[i].desc;
            s_host = s_found[i].addr;
            s_instance = desc->instance;
            s_hosting = 0;
            s_host_heard = plat_time_ms();
            s_open = 1;
            return DP_OK;
        }
    }
    return DPERR_NOSESSIONS;
}

dp_result dp_close(void)
{
    s_open = 0;
    s_hosting = 0;
    s_count = 0;
    s_local_id = 0;
    s_tail = s_head;
    return DP_OK;
}

dp_result dp_create_player(dpid *id, const struct dp_name *name)
{
    const char *text = name != NULL && name->short_name != NULL ? name->short_name : "";
    struct player *pl;
    u32 start, last;

    if (!s_open) {
        return DPERR_INVALIDOBJECT;
    }
    if (s_hosting) {
        pl = add_player(s_next_id++, text, NULL);
        if (pl == NULL) {
            return DPERR_CANTCREATEPLAYER;
        }
        pl->local = 1;
        pl->host = 1;
        s_local_id = pl->id;
        *id = pl->id;
        return DP_OK;
    }
    /* A client asks the host, again until the answer comes. */
    {
        u8 payload[DP_NAME_SIZE];

        memset(payload, 0, sizeof(payload));
        snprintf((char *)payload, sizeof(payload), "%s", text);
        s_nonce = random_u32();
        s_welcome_id = 0;
        start = plat_time_ms();
        last = start - JOIN_RETRY_MS;
        while (s_welcome_id == 0 && plat_time_ms() - start < JOIN_MS) {
            if (plat_time_ms() - last >= JOIN_RETRY_MS) {
                last = plat_time_ms();
                send_packet(&s_host, PKT_JOIN, s_nonce, 0, payload, sizeof(payload));
            }
            plat_net_wait(20);
            dp_pump();
        }
    }
    if (s_welcome_id == 0) {
        return DPERR_CANTCREATEPLAYER;
    }
    pl = add_player(s_welcome_id, text, NULL);
    if (pl == NULL) {
        return DPERR_CANTCREATEPLAYER;
    }
    pl->local = 1;
    s_local_id = s_welcome_id;
    s_host_heard = plat_time_ms();
    *id = s_local_id;
    return DP_OK;
}

dp_result dp_destroy_player(dpid id)
{
    struct player *pl = find_player(id);
    int i, k;

    if (pl == NULL) {
        return DPERR_INVALIDPLAYER;
    }
    if (pl->local && !s_lost) {
        /* Three times, as the datagrams are not guaranteed. */
        for (k = 0; k < 3; k++) {
            if (s_hosting) {
                for (i = 0; i < s_count; i++) {
                    if (!s_players[i].local) {
                        send_packet(&s_players[i].addr, PKT_LEAVE, id, s_players[i].id, NULL, 0);
                    }
                }
            } else {
                send_packet(&s_host, PKT_LEAVE, id, 0, NULL, 0);
            }
        }
    }
    if (pl->local) {
        s_local_id = 0;
    }
    remove_player(pl);
    return DP_OK;
}

dp_result dp_enum_players(dp_enum_players_fn fn, void *context, u32 flags)
{
    struct dp_name name;
    int i;

    if (!s_open) {
        return DPERR_INVALIDOBJECT;
    }
    dp_pump();
    for (i = 0; i < s_count; i++) {
        const struct player *pl = &s_players[i];

        if (((flags & DPENUMPLAYERS_REMOTE) && pl->local)
            || ((flags & DPENUMPLAYERS_LOCAL) && !pl->local)) {
            continue;
        }
        name.size = sizeof(name);
        name.flags = 0;
        name.short_name = pl->name;
        name.long_name = NULL;
        if (!fn(pl->id, DPPLAYERTYPE_PLAYER, &name, 0, context)) {
            break;
        }
    }
    return DP_OK;
}

static void fill_caps(struct dp_caps *caps, int host)
{
    caps->flags = host ? DPCAPS_ISHOST : 0;
    caps->max_buffer_size = DP_MAX_MESSAGE;
    caps->max_queue_size = 0;
    caps->max_players = DP_MAX_PLAYERS;
    caps->hundred_baud = 1000;
    caps->latency = 50;
    caps->max_local_players = 1;
    caps->header_length = PKT_HEADER;
    caps->timeout = TIMEOUT_MS;
}

dp_result dp_get_caps(struct dp_caps *caps, u32 flags)
{
    (void)flags;
    if (!s_open) {
        return DPERR_INVALIDOBJECT;
    }
    if (caps->size != sizeof(*caps)) {
        return DPERR_INVALIDPARAMS;
    }
    fill_caps(caps, s_hosting);
    return DP_OK;
}

dp_result dp_get_player_caps(dpid id, struct dp_caps *caps, u32 flags)
{
    const struct player *pl = find_player(id);

    (void)flags;
    if (pl == NULL) {
        return DPERR_INVALIDPLAYER;
    }
    if (caps->size != sizeof(*caps)) {
        return DPERR_INVALIDPARAMS;
    }
    fill_caps(caps, pl->host);
    return DP_OK;
}

dp_result dp_send(dpid from, dpid to, u32 flags, const void *data, u32 size)
{
    const struct player *pl;
    int i;

    (void)flags;
    if (!s_open || s_local_id == 0) {
        return DPERR_INVALIDOBJECT;
    }
    if (s_lost) {
        return DPERR_NOCONNECTION;
    }
    if (size > DP_MAX_MESSAGE) {
        return DPERR_SENDTOOBIG;
    }
    if (to == DPID_ALLPLAYERS) {
        for (i = 0; i < s_count; i++) {
            if (!s_players[i].local) {
                send_packet(&s_players[i].addr, PKT_DATA, from, to, data, size);
            }
        }
        return DP_OK;
    }
    pl = find_player(to);
    if (pl == NULL) {
        return DPERR_INVALIDPLAYER;
    }
    if (pl->local) {
        queue_message(from, to, data, size);
    } else {
        send_packet(&pl->addr, PKT_DATA, from, to, data, size);
    }
    return DP_OK;
}

dp_result dp_receive(dpid *from, dpid *to, u32 flags, void *data, u32 *size)
{
    unsigned i;

    if (!s_created) {
        return DPERR_INVALIDOBJECT;
    }
    dp_pump();
    for (i = s_tail; i != s_head; i++) {
        struct message *m = &s_queue[i % QUEUE];

        if (((flags & DPRECEIVE_TOPLAYER) && m->to != *to)
            || ((flags & DPRECEIVE_FROMPLAYER) && m->from != *from)) {
            continue;
        }
        if (*size < m->size) {
            *size = m->size;
            return DPERR_BUFFERTOOSMALL;
        }
        *from = m->from;
        *to = m->to;
        *size = m->size;
        memcpy(data, m->data, m->size);
        /* Out of the queue; those before it move up. */
        for (; i != s_tail; i--) {
            s_queue[i % QUEUE] = s_queue[(i - 1) % QUEUE];
        }
        s_tail++;
        return DP_OK;
    }
    return DPERR_NOMESSAGES;
}
