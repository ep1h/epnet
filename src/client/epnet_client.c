#include "epnet_client.h"

#include "epnet_connection.h"
#include "epnet_platform.h"
#include "epnet_protocol.h"

#include <stdlib.h>
#include <string.h>

/* Internal client struct */

#define EVENT_QUEUE_SIZE EPNET_EVENT_QUEUE_SIZE

struct epnet_client {
    epnet_socket_t sock;
    epnet_address_t server_addr;
    epnet_connection_t conn;

    uint8_t client_id;
    double connect_time; /* time of last connect attempt */
    int connect_retries;

    /* Event queue (ring buffer) */
    epnet_event_t events[EVENT_QUEUE_SIZE];
    int event_head;
    int event_tail;
};

/* Event queue helpers */

static void client_push_event(epnet_client_t* c, const epnet_event_t* ev)
{
    int next = (c->event_head + 1) % EVENT_QUEUE_SIZE;
    if (next == c->event_tail) {
        /* Queue full - drop oldest */
        c->event_tail = (c->event_tail + 1) % EVENT_QUEUE_SIZE;
    }
    c->events[c->event_head] = *ev;
    c->event_head = next;
}

/* Internal send helpers */

static void client_send_raw(
    epnet_client_t* c, uint8_t type, const uint8_t* payload, int payload_len
)
{
    double now = epnet_time_now();
    uint8_t buf[EPNET_MAX_PACKET_SIZE];
    epnet_packet_header_t hdr;
    epnet_connection_prepare_header(&c->conn, type, now, &hdr);
    epnet_packet_header_write(buf, &hdr);

    if (payload && payload_len > 0) {
        memcpy(buf + EPNET_HEADER_SIZE, payload, (size_t)payload_len);
    }

    epnet_socket_send(
        c->sock, c->server_addr, buf, EPNET_HEADER_SIZE + payload_len
    );
}

static void client_send_connect_req(epnet_client_t* c)
{
    uint8_t payload[EPNET_CONNECT_REQ_SIZE];
    epnet_write_connect_req(payload, c->conn.challenge_token);
    client_send_raw(c, EPNET_PKT_CONNECT_REQ, payload, EPNET_CONNECT_REQ_SIZE);
    c->connect_time = epnet_time_now();
}

static void client_send_disconnect(epnet_client_t* c)
{
    uint8_t payload[EPNET_DISCONNECT_SIZE];
    epnet_write_u64(payload, c->conn.challenge_token);
    for (int i = 0; i < EPNET_DISCONNECT_SENDS; i++) {
        client_send_raw(
            c, EPNET_PKT_DISCONNECT, payload, EPNET_DISCONNECT_SIZE
        );
    }
}

/* Packet dispatch */

static void client_handle_connect_ack(
    epnet_client_t* c, const uint8_t* payload, int payload_len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTING) {
        return;
    }
    if (payload_len < EPNET_CONNECT_ACK_SIZE) {
        return;
    }

    uint64_t challenge;
    uint8_t id;
    epnet_read_connect_ack(payload, &challenge, &id);

    if (challenge != c->conn.challenge_token) {
        return; /* not our handshake */
    }

    c->conn.state = EPNET_CONN_CONNECTED;
    c->client_id = id;

    epnet_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_EVENT_CONNECTED;
    ev.client_id = id;
    client_push_event(c, &ev);
}

static void client_handle_connect_deny(
    epnet_client_t* c, const uint8_t* payload, int payload_len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTING) {
        return;
    }

    c->conn.state = EPNET_CONN_DISCONNECTED;

    epnet_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_EVENT_DISCONNECTED;
    ev.data.disconnect_reason = (payload_len >= 1) ? payload[0] : 0;
    client_push_event(c, &ev);
}

static void client_handle_disconnect(
    epnet_client_t* c, const uint8_t* payload, int payload_len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTED) {
        return;
    }
    if (payload_len < EPNET_DISCONNECT_SIZE) {
        return;
    }

    uint64_t token = epnet_read_u64(payload);
    if (token != c->conn.challenge_token) {
        return;
    }

    c->conn.state = EPNET_CONN_DISCONNECTED;

    epnet_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_EVENT_DISCONNECTED;
    ev.data.disconnect_reason = 0;
    client_push_event(c, &ev);
}

static void client_handle_reliable_msg(
    epnet_client_t* c, const uint8_t* payload, int payload_len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTED) {
        return;
    }

    uint16_t msg_id;
    epnet_event_t ev;
    memset(&ev, 0, sizeof(ev));

    if (epnet_read_reliable_msg(
            payload, payload_len, &msg_id, ev.data.reliable.data,
            &ev.data.reliable.len
        ) < 0) {
        return;
    }

    if (epnet_connection_reliable_is_dup(&c->conn, msg_id)) {
        return;
    }

    ev.type = EPNET_EVENT_RELIABLE;
    client_push_event(c, &ev);
}

/* Client API */

static uint64_t generate_challenge(void)
{
    uint64_t token;
    epnet_random_bytes(&token, sizeof(token));
    return token;
}

epnet_client_t* epnet_client_create(void)
{
    epnet_client_t* c = calloc(1, sizeof(epnet_client_t));
    if (!c) {
        return NULL;
    }

    c->sock = epnet_socket_open(0); /* ephemeral port */
    if (c->sock == EPNET_INVALID_SOCKET) {
        free(c);
        return NULL;
    }

    epnet_connection_init(&c->conn);
    return c;
}

void epnet_client_destroy(epnet_client_t* c)
{
    if (!c) {
        return;
    }
    if (c->conn.state == EPNET_CONN_CONNECTED) {
        client_send_disconnect(c);
    }
    epnet_socket_close(c->sock);
    free(c);
}

int epnet_client_connect_addr(epnet_client_t* c, epnet_address_t addr)
{
    if (c->conn.state != EPNET_CONN_DISCONNECTED) {
        return -1;
    }

    c->server_addr = addr;

    epnet_connection_reset(&c->conn);
    c->conn.state = EPNET_CONN_CONNECTING;
    c->conn.challenge_token = generate_challenge();
    c->connect_retries = 0;
    c->event_head = 0;
    c->event_tail = 0;

    client_send_connect_req(c);
    return 0;
}

int epnet_client_connect(epnet_client_t* c, const char* host, uint16_t port)
{
    if (c->conn.state != EPNET_CONN_DISCONNECTED) {
        return -1;
    }

    epnet_address_t addr;
    if (epnet_address_from_string(host, port, &addr) != 0) {
        return -1;
    }

    return epnet_client_connect_addr(c, addr);
}

void epnet_client_disconnect(epnet_client_t* c)
{
    if (c->conn.state == EPNET_CONN_CONNECTED) {
        client_send_disconnect(c);
    }
    c->conn.state = EPNET_CONN_DISCONNECTED;

    epnet_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EPNET_EVENT_DISCONNECTED;
    ev.data.disconnect_reason = 0;
    client_push_event(c, &ev);
}

void epnet_client_update(epnet_client_t* c, double dt)
{
    (void)dt;
    double now = epnet_time_now();

    /* Drain all pending datagrams */
    uint8_t buf[EPNET_MAX_PACKET_SIZE];
    epnet_address_t from;
    int received;

    while ((received = epnet_socket_recv(c->sock, &from, buf, sizeof(buf))) > 0
    ) {
        /* Validate sender */
        if (!epnet_address_equal(from, c->server_addr)) {
            continue;
        }

        /* Parse header */
        epnet_packet_header_t hdr;
        if (!epnet_packet_header_read(buf, (size_t)received, &hdr)) {
            continue;
        }

        /* Process ack tracking */
        epnet_connection_process_header(&c->conn, &hdr, now);

        /* Dispatch by type */
        const uint8_t* payload = buf + EPNET_HEADER_SIZE;
        int payload_len = received - EPNET_HEADER_SIZE;

        switch (hdr.type) {
        case EPNET_PKT_CONNECT_ACK:
            client_handle_connect_ack(c, payload, payload_len);
            break;
        case EPNET_PKT_CONNECT_DENY:
            client_handle_connect_deny(c, payload, payload_len);
            break;
        case EPNET_PKT_DISCONNECT:
            client_handle_disconnect(c, payload, payload_len);
            break;
        case EPNET_PKT_RELIABLE_MSG:
            client_handle_reliable_msg(c, payload, payload_len);
            break;
        case EPNET_PKT_HEARTBEAT:
            break;
        default:
            /* Application packet - pass through as raw event */
            if (hdr.type >= 0x10 && c->conn.state == EPNET_CONN_CONNECTED &&
                payload_len > 0 && payload_len <= EPNET_MAX_PAYLOAD) {
                epnet_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EPNET_EVENT_PACKET;
                ev.data.packet.pkt_type = hdr.type;
                ev.data.packet.len = payload_len;
                memcpy(ev.data.packet.data, payload, (size_t)payload_len);
                client_push_event(c, &ev);
            }
            break;
        }
    }

    /* Connection state management */
    if (c->conn.state == EPNET_CONN_CONNECTING) {
        /* Retry connect request */
        if ((now - c->connect_time) >= EPNET_CONNECT_RETRY_SEC) {
            c->connect_retries++;
            if (c->connect_retries >= EPNET_CONNECT_MAX_TRIES) {
                c->conn.state = EPNET_CONN_DISCONNECTED;
                epnet_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EPNET_EVENT_DISCONNECTED;
                client_push_event(c, &ev);
            } else {
                client_send_connect_req(c);
            }
        }
    } else if (c->conn.state == EPNET_CONN_CONNECTED) {
        /* Check timeout */
        if (epnet_connection_is_timed_out(&c->conn, now, EPNET_TIMEOUT_SEC)) {
            c->conn.state = EPNET_CONN_DISCONNECTED;
            epnet_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EPNET_EVENT_DISCONNECTED;
            client_push_event(c, &ev);
            return;
        }

        /* Send heartbeat if idle */
        if ((now - c->conn.last_send_time) >= EPNET_HEARTBEAT_SEC) {
            client_send_raw(c, EPNET_PKT_HEARTBEAT, NULL, 0);
        }

        /* Process reliable resend queue */
        epnet_reliable_entry_t* re;
        while ((re = epnet_reliable_get_pending(&c->conn.reliable, now)) != NULL
        ) {
            uint8_t payload[4 + EPNET_RELIABLE_MAX_DATA];
            int plen = epnet_write_reliable_msg(
                payload, re->msg_id, re->data, re->data_len
            );
            client_send_raw(c, EPNET_PKT_RELIABLE_MSG, payload, plen);
            re->sent_on_seq = c->conn.local_seq - 1;
            re->last_send_time = now;
        }
    }
}

void epnet_client_send(
    epnet_client_t* c, uint8_t pkt_type, const void* data, int len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTED) {
        return;
    }
    if (len < 0 || len > EPNET_MAX_PAYLOAD) {
        return;
    }
    client_send_raw(c, pkt_type, data, len);
}

void epnet_client_send_reliable(
    epnet_client_t* c, const uint8_t* data, uint16_t len
)
{
    if (c->conn.state != EPNET_CONN_CONNECTED) {
        return;
    }
    epnet_reliable_push(&c->conn.reliable, data, len);
}

int epnet_client_poll_events(epnet_client_t* c, epnet_event_t* event_out)
{
    if (c->event_tail == c->event_head) {
        return 0;
    }
    *event_out = c->events[c->event_tail];
    c->event_tail = (c->event_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

epnet_client_state_t epnet_client_get_state(const epnet_client_t* c)
{
    /* Map internal state to public enum */
    switch (c->conn.state) {
    case EPNET_CONN_CONNECTING:
        return EPNET_STATE_CONNECTING;
    case EPNET_CONN_CONNECTED:
        return EPNET_STATE_CONNECTED;
    case EPNET_CONN_DISCONNECTING:
        return EPNET_STATE_DISCONNECTING;
    default:
        return EPNET_STATE_DISCONNECTED;
    }
}

float epnet_client_get_rtt(const epnet_client_t* c)
{
    return (float)c->conn.rtt;
}

uint8_t epnet_client_get_id(const epnet_client_t* c)
{
    return c->client_id;
}
