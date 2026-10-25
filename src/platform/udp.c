/*
 * platform/udp.c - the UDP socket for network play (platform.h,
 * plat_net_*), on BSD sockets (macOS, Linux, Android, iOS) and Winsock
 * (Windows). The web's and the Vita's hosts have their own.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define CLOSE_SOCKET close
#endif

#include <string.h>

#include "platform/platform.h"

static SOCKET s_sock = INVALID_SOCKET;

static void to_sockaddr(const struct plat_net_addr *a, struct sockaddr_in *sa)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = htonl(a->ip);
    sa->sin_port = htons(a->port);
}

int plat_net_open(u16 port)
{
    struct sockaddr_in sa;
    int on = 1;

#ifdef _WIN32
    static int s_started;
    u_long nb = 1;

    if (!s_started) {
        WSADATA wsa;

        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return 0;
        }
        s_started = 1;
    }
#endif
    plat_net_close();
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock == INVALID_SOCKET) {
        return 0;
    }
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
#ifdef _WIN32
    ioctlsocket(s_sock, FIONBIO, &nb);
#else
    fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s_sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        plat_net_close();
        return 0;
    }
    return 1;
}

void plat_net_close(void)
{
    if (s_sock != INVALID_SOCKET) {
        CLOSE_SOCKET(s_sock);
        s_sock = INVALID_SOCKET;
    }
}

int plat_net_send(const struct plat_net_addr *to, const void *buf, int size)
{
    struct sockaddr_in sa;

    if (s_sock == INVALID_SOCKET) {
        return 0;
    }
    to_sockaddr(to, &sa);
    return sendto(s_sock, (const char *)buf, size, 0, (struct sockaddr *)&sa, sizeof(sa)) == size;
}

int plat_net_recv(struct plat_net_addr *from, void *buf, int size)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    int n;

    if (s_sock == INVALID_SOCKET) {
        return -1;
    }
    n = (int)recvfrom(s_sock, (char *)buf, size, 0, (struct sockaddr *)&sa, &len);
    if (n < 0) {
#ifdef _WIN32
        int e = WSAGetLastError();

        /* A port that refused an earlier datagram, on Windows. */
        return e == WSAEWOULDBLOCK || e == WSAECONNRESET || e == WSAEMSGSIZE ? 0 : -1;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED
            || errno == EINTR ? 0 : -1;
#endif
    }
    from->ip = ntohl(sa.sin_addr.s_addr);
    from->port = ntohs(sa.sin_port);
    return n;
}

void plat_net_wait(u32 ms)
{
    fd_set set;
    struct timeval tv;

    if (s_sock == INVALID_SOCKET) {
        return;
    }
    FD_ZERO(&set);
    FD_SET(s_sock, &set);
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)(ms % 1000) * 1000;
    select((int)s_sock + 1, &set, NULL, NULL, &tv);
}

int plat_net_resolve(const char *name, u32 *ip)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(name, NULL, &hints, &res) != 0 || res == NULL) {
        return 0;
    }
    *ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return 1;
}
