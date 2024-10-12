/* Shared types, constants, library lifecycle for epnet */
#ifndef EPNET_H
#define EPNET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Network address (IPv4) */

typedef struct {
    uint32_t host; /* IPv4 address, network byte order */
    uint16_t port; /* port, network byte order         */
} epnet_address_t;

#ifndef EPNET_MAX_CLIENTS
#define EPNET_MAX_CLIENTS 16
#endif
#ifndef EPNET_MAX_PAYLOAD
#define EPNET_MAX_PAYLOAD 1220
#endif
#ifndef EPNET_RELIABLE_MAX_DATA
#define EPNET_RELIABLE_MAX_DATA 256
#endif

/* Library lifecycle (required on Windows for WSAStartup/WSACleanup) */

int epnet_init(void);
void epnet_shutdown(void);

/* Address helpers */

int epnet_address_from_string(
    const char* host, uint16_t port, epnet_address_t* out
);

#ifdef __cplusplus
}
#endif

#endif /* EPNET_H */
