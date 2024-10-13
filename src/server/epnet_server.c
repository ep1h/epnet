#include "epnet_server.h"

#include "epnet_connection.h"
#include "epnet_platform.h"
#include "epnet_protocol.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Internal server types */

#define EVENT_QUEUE_SIZE EPNET_EVENT_QUEUE_SIZE

typedef struct {
    epnet_connection_t conn;
    epnet_address_t addr;
    bool active;
} epnet_server_slot_t;

struct epnet_server {
    epnet_socket_t sock;
    epnet_server_slot_t clients[EPNET_MAX_CLIENTS];
    int max_clients;
    int num_connected;

    /* Event queue */
    epnet_srv_event_t events[EVENT_QUEUE_SIZE];
    int event_head;
    int event_tail;
};

/* Event queue helpers */

static void server_push_event(epnet_server_t* s, const epnet_srv_event_t* ev)
{
    int next = (s->event_head + 1) % EVENT_QUEUE_SIZE;
    if (next == s->event_tail) {
        /* Queue full - drop oldest */
        s->event_tail = (s->event_tail + 1) % EVENT_QUEUE_SIZE;
    }
    s->events[s->event_head] = *ev;
    s->event_head = next;
}

/* Internal send helpers */

static void server_send_to(
    epnet_server_t* s, int slot, uint8_t type, const uint8_t* payload,
    int payload_len
)
{
    double now = epnet_time_now();
    uint8_t buf[EPNET_MAX_PACKET_SIZE];
    epnet_packet_header_t hdr;
    epnet_connection_prepare_header(&s->clients[slot].conn, type, now, &hdr);
    epnet_packet_header_write(buf, &hdr);

    if (payload && payload_len > 0) {
        memcpy(buf + EPNET_HEADER_SIZE, payload, (size_t)payload_len);
    }

    epnet_socket_send(
        s->sock, s->clients[slot].addr, buf, EPNET_HEADER_SIZE + payload_len
    );
}

/* Connection management */

static int server_find_slot_by_addr(epnet_server_t* s, epnet_address_t addr)
{
    for (int i = 0; i < s->max_clients; i++) {
        if (s->clients[i].active &&
            epnet_address_equal(s->clients[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

static int server_find_free_slot(epnet_server_t* s)
{
    for (int i = 0; i < s->max_clients; i++) {
        if (!s->clients[i].active) {
            return i;
        }
    }
    return -1;
}

static void server_accept_client(
    epnet_server_t* s, epnet_address_t addr, uint64_t challenge,
    const epnet_packet_header_t* hdr, double now
)
{
    /* Check if already connected (resend ACK) */
    int existing = server_find_slot_by_addr(s, addr);
    if (existing >= 0) {
        epnet_server_slot_t* cl = &s->clients[existing];
        if (cl->conn.challenge_token == challenge) {
            uint8_t payload[EPNET_CONNECT_ACK_SIZE];
            epnet_write_connect_ack(payload, challenge, (uint8_t)existing);
            server_send_to(
                s, existing, EPNET_PKT_CONNECT_ACK, payload,
                EPNET_CONNECT_ACK_SIZE
            );
        }
        return;
    }

    int slot = server_find_free_slot(s);
    if (slot < 0) {
        /* Server full - send deny directly */
        uint8_t buf[EPNET_HEADER_SIZE + EPNET_CONNECT_DENY_SIZE];
        epnet_packet_header_t deny_hdr = {
            .protocol_id = EPNET_PROTOCOL_ID,
            .sequence = 0,
            .ack = 0,
            .ack_bits = 0,
            .type = EPNET_PKT_CONNECT_DENY,
        };
        epnet_packet_header_write(buf, &deny_hdr);
        buf[EPNET_HEADER_SIZE] = EPNET_DENY_FULL;
        epnet_socket_send(
            s->sock, addr, buf, EPNET_HEADER_SIZE + EPNET_CONNECT_DENY_SIZE
        );
        return;
    }

    /* Accept the client */
    epnet_server_slot_t* cl = &s->clients[slot];
    epnet_connection_init(&cl->conn);
    cl->conn.state = EPNET_CONN_CONNECTED;
    cl->conn.challenge_token = challenge;
    cl->conn.last_recv_time = now;
    cl->addr = addr;
    cl->active = true;
    s->num_connected++;

    /* Process the received header's ack info */
    epnet_connection_process_header(&cl->conn, hdr, now);

    /* Send ACK */
    uint8_t payload[EPNET_CONNECT_ACK_SIZE];
    epnet_write_connect_ack(payload, challenge, (uint8_t)slot);
    server_send_to(
        s, slot, EPNET_PKT_CONNECT_ACK, payload, EPNET_CONNECT_ACK_SIZE
    );

    /* Push event */
    epnet_srv_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_SRV_EVENT_CLIENT_JOIN;
    ev.client_id = (uint8_t)slot;
    server_push_event(s, &ev);
}

static void server_remove_client(epnet_server_t* s, int slot)
{
    if (!s->clients[slot].active) {
        return;
    }

    s->clients[slot].active = false;
    epnet_connection_reset(&s->clients[slot].conn);
    s->num_connected--;

    epnet_srv_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_SRV_EVENT_CLIENT_LEAVE;
    ev.client_id = (uint8_t)slot;
    server_push_event(s, &ev);
}

/* Packet dispatch */

static void server_handle_disconnect(
    epnet_server_t* s, int slot, const uint8_t* payload, int payload_len
)
{
    if (payload_len < EPNET_DISCONNECT_SIZE) {
        return;
    }
    uint64_t token = epnet_read_u64(payload);
    if (token != s->clients[slot].conn.challenge_token) {
        return;
    }
    server_remove_client(s, slot);
}

static void server_handle_reliable_msg(
    epnet_server_t* s, int slot, const uint8_t* payload, int payload_len
)
{
    uint16_t msg_id;
    epnet_srv_event_t ev;
    memset(&ev, 0, sizeof(ev));

    if (epnet_read_reliable_msg(
            payload, payload_len, &msg_id, ev.data.reliable.data,
            &ev.data.reliable.len
        ) < 0) {
        return;
    }

    if (epnet_connection_reliable_is_dup(&s->clients[slot].conn, msg_id)) {
        return;
    }

    ev.type = EPNET_SRV_EVENT_RELIABLE;
    ev.client_id = (uint8_t)slot;
    server_push_event(s, &ev);
}

/* Server API */

epnet_server_t* epnet_server_create(uint16_t port, int max_clients)
{
    if (max_clients > EPNET_MAX_CLIENTS) {
        max_clients = EPNET_MAX_CLIENTS;
    }

    epnet_server_t* s = calloc(1, sizeof(epnet_server_t));
    if (!s) {
        return NULL;
    }

    s->sock = epnet_socket_open(port);
    if (s->sock == EPNET_INVALID_SOCKET) {
        free(s);
        return NULL;
    }

    s->max_clients = max_clients;

    return s;
}

void epnet_server_destroy(epnet_server_t* s)
{
    if (!s) {
        return;
    }

    /* Notify all connected clients */
    for (int i = 0; i < s->max_clients; i++) {
        if (s->clients[i].active) {
            epnet_server_disconnect_client(s, (uint8_t)i);
        }
    }

    epnet_socket_close(s->sock);
    free(s);
}

void epnet_server_update(epnet_server_t* s, double dt)
{
    (void)dt;
    double now = epnet_time_now();

    /* Drain all pending datagrams */
    uint8_t buf[EPNET_MAX_PACKET_SIZE];
    epnet_address_t from;
    int received;

    while ((received = epnet_socket_recv(s->sock, &from, buf, sizeof(buf))) > 0
    ) {
        epnet_packet_header_t hdr;
        if (!epnet_packet_header_read(buf, (size_t)received, &hdr)) {
            continue;
        }

        const uint8_t* payload = buf + EPNET_HEADER_SIZE;
        int payload_len = received - EPNET_HEADER_SIZE;

        /* Find existing connection */
        int slot = server_find_slot_by_addr(s, from);

        if (slot < 0) {
            /* Unknown sender - only accept CONNECT_REQ */
            if (hdr.type == EPNET_PKT_CONNECT_REQ &&
                payload_len >= EPNET_CONNECT_REQ_SIZE) {
                uint64_t challenge = epnet_read_connect_req(payload);
                server_accept_client(s, from, challenge, &hdr, now);
            }
            continue;
        }

        /* Known client - process ack tracking */
        epnet_connection_process_header(&s->clients[slot].conn, &hdr, now);

        switch (hdr.type) {
        case EPNET_PKT_CONNECT_REQ:
            /* Resend ACK (handled in accept_client) */
            if (payload_len >= EPNET_CONNECT_REQ_SIZE) {
                uint64_t challenge = epnet_read_connect_req(payload);
                server_accept_client(s, from, challenge, &hdr, now);
            }
            break;
        case EPNET_PKT_DISCONNECT:
            server_handle_disconnect(s, slot, payload, payload_len);
            break;
        case EPNET_PKT_RELIABLE_MSG:
            server_handle_reliable_msg(s, slot, payload, payload_len);
            break;
        case EPNET_PKT_HEARTBEAT:
            /* Ack tracking already done */
            break;
        default:
            /* Application packet - pass through as event */
            if (hdr.type >= 0x10 && s->clients[slot].active &&
                payload_len > 0 && payload_len <= (int)EPNET_MAX_PAYLOAD) {
                epnet_srv_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EPNET_SRV_EVENT_PACKET;
                ev.client_id = (uint8_t)slot;
                ev.data.packet.pkt_type = hdr.type;
                ev.data.packet.len = payload_len;
                memcpy(ev.data.packet.data, payload, (size_t)payload_len);
                server_push_event(s, &ev);
            }
            break;
        }
    }

    /* Check timeouts and send heartbeats */
    for (int i = 0; i < s->max_clients; i++) {
        if (!s->clients[i].active) {
            continue;
        }

        epnet_server_slot_t* cl = &s->clients[i];

        if (epnet_connection_is_timed_out(&cl->conn, now, EPNET_TIMEOUT_SEC)) {
            server_remove_client(s, i);
            continue;
        }

        /* Heartbeat if idle */
        if ((now - cl->conn.last_send_time) >= EPNET_HEARTBEAT_SEC) {
            server_send_to(s, i, EPNET_PKT_HEARTBEAT, NULL, 0);
        }

        /* Process reliable resend queue */
        epnet_reliable_entry_t* re;
        while ((re = epnet_reliable_get_pending(&cl->conn.reliable, now)) !=
               NULL) {
            uint8_t rpayload[4 + EPNET_RELIABLE_MAX_DATA];
            int plen = epnet_write_reliable_msg(
                rpayload, re->msg_id, re->data, re->data_len
            );
            server_send_to(s, i, EPNET_PKT_RELIABLE_MSG, rpayload, plen);
            re->sent_on_seq = cl->conn.local_seq - 1;
            re->last_send_time = now;
        }
    }
}

void epnet_server_send(
    epnet_server_t* s, uint8_t client_id, uint8_t pkt_type, const void* data,
    int len
)
{
    if (client_id >= EPNET_MAX_CLIENTS || !s->clients[client_id].active) {
        return;
    }
    if (len < 0 || len > (int)EPNET_MAX_PAYLOAD) {
        return;
    }
    server_send_to(s, client_id, pkt_type, data, len);
}

void epnet_server_broadcast(
    epnet_server_t* s, uint8_t pkt_type, const void* data, int len
)
{
    if (len < 0 || len > (int)EPNET_MAX_PAYLOAD) {
        return;
    }
    for (int i = 0; i < s->max_clients; i++) {
        if (s->clients[i].active) {
            server_send_to(s, i, pkt_type, data, len);
        }
    }
}

void epnet_server_send_reliable(
    epnet_server_t* s, uint8_t client_id, const uint8_t* data, uint16_t len
)
{
    if (client_id >= EPNET_MAX_CLIENTS || !s->clients[client_id].active) {
        return;
    }
    epnet_reliable_push(&s->clients[client_id].conn.reliable, data, len);
}

void epnet_server_disconnect_client(epnet_server_t* s, uint8_t client_id)
{
    if (client_id >= EPNET_MAX_CLIENTS || !s->clients[client_id].active) {
        return;
    }

    /* Send disconnect packets */
    uint8_t payload[EPNET_DISCONNECT_SIZE];
    epnet_write_u64(payload, s->clients[client_id].conn.challenge_token);
    for (int i = 0; i < EPNET_DISCONNECT_SENDS; i++) {
        server_send_to(
            s, client_id, EPNET_PKT_DISCONNECT, payload, EPNET_DISCONNECT_SIZE
        );
    }

    server_remove_client(s, client_id);
}

int epnet_server_poll_events(epnet_server_t* s, epnet_srv_event_t* event_out)
{
    if (s->event_tail == s->event_head) {
        return 0;
    }
    *event_out = s->events[s->event_tail];
    s->event_tail = (s->event_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}
