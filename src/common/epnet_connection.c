#include "epnet_connection.h"

#include <string.h>

/* Connection lifecycle */

void epnet_connection_init(epnet_connection_t* conn)
{
    memset(conn, 0, sizeof(*conn));
    conn->state = EPNET_CONN_DISCONNECTED;
}

void epnet_connection_reset(epnet_connection_t* conn)
{
    epnet_connection_init(conn);
}

/* Ack-bitfield algorithm */

void epnet_connection_prepare_header(
    epnet_connection_t* conn, uint8_t type, double current_time,
    epnet_packet_header_t* hdr_out
)
{
    hdr_out->protocol_id = EPNET_PROTOCOL_ID;
    hdr_out->sequence = conn->local_seq;
    hdr_out->ack = conn->remote_seq;
    hdr_out->ack_bits = conn->recv_bits;
    hdr_out->type = type;

    /* Record in sent buffer */
    int idx = conn->local_seq % EPNET_SENT_BUFFER_SIZE;
    conn->sent_buffer[idx].sequence = conn->local_seq;
    conn->sent_buffer[idx].send_time = current_time;
    conn->sent_buffer[idx].acked = false;

    conn->local_seq++;
    conn->last_send_time = current_time;
}

void epnet_connection_process_header(
    epnet_connection_t* conn, const epnet_packet_header_t* hdr,
    double current_time
)
{
    conn->last_recv_time = current_time;

    /* Update remote sequence and recv_bits */
    if (epnet_seq_greater(hdr->sequence, conn->remote_seq)) {
        /* New highest sequence received */
        uint16_t diff = hdr->sequence - conn->remote_seq;
        if (diff <= 32) {
            /* Shift existing bits and set the old remote_seq bit */
            conn->recv_bits =
                (diff < 32 ? conn->recv_bits << diff : 0) | (1u << (diff - 1));
        } else {
            /* Gap too large, reset bitfield */
            conn->recv_bits = 0;
        }
        conn->remote_seq = hdr->sequence;
    } else {
        /* Older or duplicate packet - set appropriate bit */
        uint16_t diff = conn->remote_seq - hdr->sequence;
        if (diff > 0 && diff <= 32) {
            conn->recv_bits |= (1u << (diff - 1));
        }
    }

    /* Process ack / ack_bits to mark our sent packets */
    /* The remote is acknowledging our packets */

    /* Check the single ack field */
    {
        int idx = hdr->ack % EPNET_SENT_BUFFER_SIZE;
        epnet_sent_packet_info_t* info = &conn->sent_buffer[idx];
        if (info->sequence == hdr->ack && !info->acked) {
            info->acked = true;
            /* Update RTT with exponential smoothing */
            double sample = current_time - info->send_time;
            if (conn->rtt <= 0.0) {
                conn->rtt = sample;
            } else {
                conn->rtt += (sample - conn->rtt) * 0.1;
            }
            /* Notify reliable queue */
            epnet_reliable_on_ack(&conn->reliable, hdr->ack);
        }
    }

    /* Check each bit in ack_bits */
    for (int i = 0; i < 32; i++) {
        if (hdr->ack_bits & (1u << i)) {
            uint16_t seq = (uint16_t)(hdr->ack - 1 - (uint16_t)i);
            int idx = seq % EPNET_SENT_BUFFER_SIZE;
            epnet_sent_packet_info_t* info = &conn->sent_buffer[idx];
            if (info->sequence == seq && !info->acked) {
                info->acked = true;
                double sample = current_time - info->send_time;
                if (conn->rtt <= 0.0) {
                    conn->rtt = sample;
                } else {
                    conn->rtt += (sample - conn->rtt) * 0.1;
                }
                epnet_reliable_on_ack(&conn->reliable, seq);
            }
        }
    }
}

/* Timeout */

bool epnet_connection_is_timed_out(
    const epnet_connection_t* conn, double current_time, double timeout_sec
)
{
    if (conn->last_recv_time <= 0.0) {
        return false; /* never received anything yet */
    }
    return (current_time - conn->last_recv_time) >= timeout_sec;
}

/* Reliable message queue */

void epnet_reliable_push(
    epnet_reliable_queue_t* q, const uint8_t* data, uint16_t len
)
{
    if (len > EPNET_RELIABLE_MAX_DATA) {
        return; /* too large, drop silently */
    }

    /* Find a free slot */
    for (int i = 0; i < EPNET_RELIABLE_BUFFER_SIZE; i++) {
        if (!q->entries[i].pending) {
            epnet_reliable_entry_t* e = &q->entries[i];
            e->msg_id = q->next_msg_id++;
            e->sent_on_seq = 0;
            e->last_send_time = 0.0;
            e->data_len = len;
            memcpy(e->data, data, len);
            e->pending = true;
            q->count++;
            return;
        }
    }
    /* Queue full - drop. Caller should throttle. */
}

void epnet_reliable_on_ack(epnet_reliable_queue_t* q, uint16_t acked_seq)
{
    for (int i = 0; i < EPNET_RELIABLE_BUFFER_SIZE; i++) {
        epnet_reliable_entry_t* e = &q->entries[i];
        if (e->pending && e->sent_on_seq == acked_seq &&
            e->last_send_time > 0.0) {
            e->pending = false;
            q->count--;
        }
    }
}

epnet_reliable_entry_t*
epnet_reliable_get_pending(epnet_reliable_queue_t* q, double current_time)
{
    for (int i = 0; i < EPNET_RELIABLE_BUFFER_SIZE; i++) {
        epnet_reliable_entry_t* e = &q->entries[i];
        if (!e->pending) {
            continue;
        }
        /* Never sent, or resend interval elapsed */
        if (e->last_send_time <= 0.0 ||
            (current_time - e->last_send_time) >= EPNET_RELIABLE_RESEND_SEC) {
            return e;
        }
    }
    return NULL;
}

/* Reliable receive dedup (sliding window) */

bool epnet_connection_reliable_is_dup(epnet_connection_t* conn, uint16_t msg_id)
{
    if (!conn->reliable_recv_started) {
        conn->reliable_recv_started = true;
        conn->reliable_recv_seq = msg_id;
        conn->reliable_recv_bits = 0;
        return false;
    }

    /* Exact duplicate of most recent */
    if (msg_id == conn->reliable_recv_seq) {
        return true;
    }

    if (epnet_seq_greater(msg_id, conn->reliable_recv_seq)) {
        /* Newer msg_id - advance window */
        uint16_t diff = msg_id - conn->reliable_recv_seq;
        if (diff <= 32) {
            conn->reliable_recv_bits =
                (diff < 32 ? conn->reliable_recv_bits << diff : 0) |
                (1u << (diff - 1));
        } else {
            conn->reliable_recv_bits = 0;
        }
        conn->reliable_recv_seq = msg_id;
        return false;
    }

    /* Older msg_id - check window */
    uint16_t diff = conn->reliable_recv_seq - msg_id;
    if (diff <= 32) {
        if (conn->reliable_recv_bits & (1u << (diff - 1))) {
            return true; /* already received */
        }
        conn->reliable_recv_bits |= (1u << (diff - 1));
        return false; /* new, out-of-order delivery */
    }

    /* Outside window - assume already delivered */
    return true;
}
