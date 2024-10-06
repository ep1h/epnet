/* Connection tracking and reliability */
#ifndef EPNET_CONNECTION_H
#define EPNET_CONNECTION_H

#include "epnet_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EPNET_CONN_DISCONNECTED = 0,
    EPNET_CONN_CONNECTING = 1,
    EPNET_CONN_CONNECTED = 2,
    EPNET_CONN_DISCONNECTING = 3,
} epnet_conn_state_t;

/* Must be power of 2 */
#define EPNET_SENT_BUFFER_SIZE 256
#define EPNET_RELIABLE_BUFFER_SIZE 64
#ifndef EPNET_RELIABLE_MAX_DATA
#define EPNET_RELIABLE_MAX_DATA 256
#endif

/* Sent packet tracking */
typedef struct {
    double send_time;
    uint16_t sequence;
    bool acked;
} epnet_sent_packet_info_t;

/* Reliable message entry */
typedef struct {
    double last_send_time;
    uint16_t msg_id;
    uint16_t sent_on_seq; /* packet sequence that last carried this */
    uint16_t data_len;
    bool pending;
    uint8_t data[EPNET_RELIABLE_MAX_DATA];
} epnet_reliable_entry_t;

/* Reliable message queue */
typedef struct {
    epnet_reliable_entry_t entries[EPNET_RELIABLE_BUFFER_SIZE];
    uint16_t next_msg_id;
    int count; /* number of pending entries */
} epnet_reliable_queue_t;

/* Connection state */
typedef struct {
    epnet_conn_state_t state;
    uint64_t challenge_token;

    /* Sequence tracking */
    uint16_t local_seq;  /* next outgoing sequence  */
    uint16_t remote_seq; /* latest received remote   */
    uint32_t recv_bits;  /* bitfield of received     */

    /* Sent packet ring buffer for ack processing */
    epnet_sent_packet_info_t sent_buffer[EPNET_SENT_BUFFER_SIZE];

    /* Timing */
    double last_send_time;
    double last_recv_time;
    double rtt; /* smoothed RTT in seconds */

    /* Reliable message queue */
    epnet_reliable_queue_t reliable;

    /* Reliable receive dedup (sliding window) */
    uint32_t reliable_recv_bits; /* bitfield of prior 32 msg_ids       */
    uint16_t reliable_recv_seq;  /* highest msg_id delivered           */
    bool reliable_recv_started;  /* false until first reliable msg recv */
} epnet_connection_t;

/* Connection lifecycle */
void epnet_connection_init(epnet_connection_t* conn);
void epnet_connection_reset(epnet_connection_t* conn);

/* Ack-bitfield algorithm */

/*
 * Prepare outgoing header: fills sequence/ack/ack_bits, increments
 * local_seq, records the packet in the sent ring buffer.
 */
void epnet_connection_prepare_header(
    epnet_connection_t* conn, uint8_t type, double current_time,
    epnet_packet_header_t* hdr_out
);

/*
 * Process a received header: updates remote_seq, recv_bits,
 * marks our sent packets as acked via ack/ack_bits, updates RTT.
 */
void epnet_connection_process_header(
    epnet_connection_t* conn, const epnet_packet_header_t* hdr,
    double current_time
);

/* Timeout */
bool epnet_connection_is_timed_out(
    const epnet_connection_t* conn, double current_time, double timeout_sec
);

/* Reliable message API */
void epnet_reliable_push(
    epnet_reliable_queue_t* q, const uint8_t* data, uint16_t len
);

void epnet_reliable_on_ack(epnet_reliable_queue_t* q, uint16_t acked_seq);

/*
 * Return the next reliable entry due for (re)sending, or NULL.
 * Caller sends it, then sets entry->sent_on_seq and entry->last_send_time.
 */
epnet_reliable_entry_t*
epnet_reliable_get_pending(epnet_reliable_queue_t* q, double current_time);

/* Returns true if msg_id was already received (duplicate). */
bool epnet_connection_reliable_is_dup(
    epnet_connection_t* conn, uint16_t msg_id
);

#endif /* EPNET_CONNECTION_H */
