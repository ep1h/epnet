/* UDP networking API - server side */
#ifndef EPNET_SERVER_H
#define EPNET_SERVER_H

#include "epnet.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Server events */

typedef enum {
    EPNET_SRV_EVENT_CLIENT_JOIN = 1,
    EPNET_SRV_EVENT_CLIENT_LEAVE = 2,
    EPNET_SRV_EVENT_PACKET = 3,
    EPNET_SRV_EVENT_RELIABLE = 4,
} epnet_srv_event_type_t;

typedef struct {
    epnet_srv_event_type_t type;
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
    } data;
} epnet_srv_event_t;

typedef struct epnet_server epnet_server_t;

/* Server API */

epnet_server_t* epnet_server_create(uint16_t port, int max_clients);

void epnet_server_destroy(epnet_server_t* s);

/*
 * Drain incoming packets, handle connections, timeouts.
 * Call once before each tick batch.
 */
void epnet_server_update(epnet_server_t* s, double dt);

/* Send a raw application packet to a specific client. */
void epnet_server_send(
    epnet_server_t* s, uint8_t client_id, uint8_t pkt_type, const void* data,
    int len
);

/* Broadcast a raw application packet to all connected clients. */
void epnet_server_broadcast(
    epnet_server_t* s, uint8_t pkt_type, const void* data, int len
);

/* Enqueue a reliable message for a specific client. */
void epnet_server_send_reliable(
    epnet_server_t* s, uint8_t client_id, const uint8_t* data, uint16_t len
);

/* Forcibly disconnect a client. */
void epnet_server_disconnect_client(epnet_server_t* s, uint8_t client_id);

/* Dequeue the next server event. Returns 1 if available, 0 if empty. */
int epnet_server_poll_events(epnet_server_t* s, epnet_srv_event_t* event_out);

#ifdef __cplusplus
}
#endif

#endif /* EPNET_SERVER_H */
