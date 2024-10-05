/* Cross-platform abstraction */
#ifndef EPNET_PLATFORM_H
#define EPNET_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET epnet_socket_t;
#define EPNET_INVALID_SOCKET INVALID_SOCKET
#else
typedef int epnet_socket_t;
#define EPNET_INVALID_SOCKET (-1)
#endif

typedef struct {
    uint32_t host; /* IPv4 address, network byte order */
    uint16_t port; /* port, network byte order         */
} epnet_address_t;

/* Library init / shutdown */
int epnet_init(void);
void epnet_shutdown(void);

/* Socket operations */
epnet_socket_t epnet_socket_open(uint16_t port);
void epnet_socket_close(epnet_socket_t sock);

int epnet_socket_send(
    epnet_socket_t sock, epnet_address_t addr, const void* data, int len
);

int epnet_socket_recv(
    epnet_socket_t sock, epnet_address_t* addr_out, void* buf, int buf_len
);

/* Address helpers */
int epnet_address_from_string(
    const char* host, uint16_t port, epnet_address_t* out
);
bool epnet_address_equal(epnet_address_t a, epnet_address_t b);

/* High-resolution monotonic timer */
double epnet_time_now(void);

/* Sleep (seconds) */
void epnet_sleep(double seconds);

/* Cryptographic random bytes */
void epnet_random_bytes(void* buf, int len);

#endif /* EPNET_PLATFORM_H */
