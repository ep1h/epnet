#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "epnet_platform.h"

#include <string.h>

#ifdef _WIN32

/* Windows implementation */

#include <ws2tcpip.h>

int epnet_init(void)
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}

void epnet_shutdown(void)
{
    WSACleanup();
}

epnet_socket_t epnet_socket_open(uint16_t port)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return EPNET_INVALID_SOCKET;
    }

    /* Allow address reuse */
    BOOL reuse = TRUE;
    setsockopt(
        sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)
    );

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return EPNET_INVALID_SOCKET;
    }

    /* Non-blocking */
    u_long nonblocking = 1;
    ioctlsocket(sock, (long)FIONBIO, &nonblocking);

    return sock;
}

void epnet_socket_close(epnet_socket_t sock)
{
    if (sock != EPNET_INVALID_SOCKET) {
        closesocket(sock);
    }
}

int epnet_socket_send(
    epnet_socket_t sock, epnet_address_t addr, const void* data, int len
)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr.host;
    sa.sin_port = addr.port;

    int sent = sendto(
        sock, (const char*)data, len, 0, (struct sockaddr*)&sa, sizeof(sa)
    );
    return sent;
}

int epnet_socket_recv(
    epnet_socket_t sock, epnet_address_t* addr_out, void* buf, int buf_len
)
{
    struct sockaddr_in from;
    int from_len = sizeof(from);

    int received = recvfrom(
        sock, (char*)buf, buf_len, 0, (struct sockaddr*)&from, &from_len
    );

    if (received <= 0) {
        return -1;
    }

    if (addr_out) {
        addr_out->host = from.sin_addr.s_addr;
        addr_out->port = from.sin_port;
    }
    return received;
}

int epnet_address_from_string(
    const char* host, uint16_t port, epnet_address_t* out
)
{
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        return -1;
    }

    struct sockaddr_in* sa = (struct sockaddr_in*)result->ai_addr;
    out->host = sa->sin_addr.s_addr;
    out->port = htons(port);
    freeaddrinfo(result);
    return 0;
}

double epnet_time_now(void)
{
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}

void epnet_sleep(double seconds)
{
    if (seconds > 0.0) {
        Sleep((DWORD)(seconds * 1000.0));
    }
}

void epnet_random_bytes(void* buf, int len)
{
    /* RtlGenRandom (SystemFunction036) — available since XP */
    extern BOOLEAN NTAPI SystemFunction036(PVOID, ULONG);
    SystemFunction036(buf, (ULONG)len);
}

#else

/* POSIX implementation */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int epnet_init(void)
{
    return 0;
}

void epnet_shutdown(void)
{
}

epnet_socket_t epnet_socket_open(uint16_t port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return EPNET_INVALID_SOCKET;
    }

    /* Allow address reuse */
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return EPNET_INVALID_SOCKET;
    }

    /* Non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

void epnet_socket_close(epnet_socket_t sock)
{
    if (sock != EPNET_INVALID_SOCKET) {
        close(sock);
    }
}

int epnet_socket_send(
    epnet_socket_t sock, epnet_address_t addr, const void* data, int len
)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr.host;
    sa.sin_port = addr.port;

    ssize_t sent =
        sendto(sock, data, (size_t)len, 0, (struct sockaddr*)&sa, sizeof(sa));
    return (int)sent;
}

int epnet_socket_recv(
    epnet_socket_t sock, epnet_address_t* addr_out, void* buf, int buf_len
)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    ssize_t received = recvfrom(
        sock, buf, (size_t)buf_len, 0, (struct sockaddr*)&from, &from_len
    );

    if (received <= 0) {
        return -1;
    }

    if (addr_out) {
        addr_out->host = from.sin_addr.s_addr;
        addr_out->port = from.sin_port;
    }
    return (int)received;
}

int epnet_address_from_string(
    const char* host, uint16_t port, epnet_address_t* out
)
{
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        return -1;
    }

    struct sockaddr_in* sa = (struct sockaddr_in*)result->ai_addr;
    out->host = sa->sin_addr.s_addr;
    out->port = htons(port);
    freeaddrinfo(result);
    return 0;
}

double epnet_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void epnet_sleep(double seconds)
{
    if (seconds > 0.0) {
        struct timespec ts;
        ts.tv_sec = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
}

void epnet_random_bytes(void* buf, int len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, (size_t)len);
        close(fd);
    }
}

#endif /* _WIN32 */

/* Platform-independent helpers */

bool epnet_address_equal(epnet_address_t a, epnet_address_t b)
{
    return a.host == b.host && a.port == b.port;
}
