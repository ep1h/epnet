/* UDP networking API - client side */
#ifndef EPNET_CLIENT_H
#define EPNET_CLIENT_H

#include "epnet.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Connection state */

typedef enum {
    EPNET_STATE_DISCONNECTED = 0,
    EPNET_STATE_CONNECTING = 1,
    EPNET_STATE_CONNECTED = 2,
    EPNET_STATE_DISCONNECTING = 3,
} epnet_client_state_t;

/* Events */

typedef enum {
    EPNET_EVENT_CONNECTED = 1,
    EPNET_EVENT_DISCONNECTED = 2,
    EPNET_EVENT_PACKET = 3,
    EPNET_EVENT_RELIABLE = 4,
} epnet_event_type_t;

/* TODO: events embed full payload inline (~1.2KB each). Consider a
 * pointer+pool scheme if this causes cache pressure at scale. */
typedef struct {
    epnet_event_type_t type;
    uint8_t client_id;
    union {
        struct {
            uint8_t pkt_type;
            int len;
            uint8_t data[EPNET_MAX_PAYLOAD];
        } packet;
        struct {
            uint16_t len;
            uint8_t data[EPNET_RELIABLE_MAX_DATA];
        } reliable;
        uint8_t disconnect_reason;
    } data;
} epnet_event_t;

typedef struct epnet_client epnet_client_t;

/* Client API */

epnet_client_t* epnet_client_create(void);
void epnet_client_destroy(epnet_client_t* c);

int epnet_client_connect(epnet_client_t* c, const char* host, uint16_t port);
int epnet_client_connect_addr(epnet_client_t* c, epnet_address_t addr);
void epnet_client_disconnect(epnet_client_t* c);

void epnet_client_update(epnet_client_t* c, double dt);

void epnet_client_send(
    epnet_client_t* c, uint8_t pkt_type, const void* data, int len
);

void epnet_client_send_reliable(
    epnet_client_t* c, const uint8_t* data, uint16_t len
);

int epnet_client_poll_events(epnet_client_t* c, epnet_event_t* event_out);

epnet_client_state_t epnet_client_get_state(const epnet_client_t* c);
float epnet_client_get_rtt(const epnet_client_t* c);
uint8_t epnet_client_get_id(const epnet_client_t* c);

#ifdef __cplusplus
}
#endif

#endif /* EPNET_CLIENT_H */
