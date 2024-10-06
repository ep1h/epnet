/* Wire protocol definitions and serialization.
 * All multi-byte fields are network byte order on the wire.
 */
#ifndef EPNET_PROTOCOL_H
#define EPNET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Constants */
#define EPNET_PROTOCOL_ID 0x5031 /* "P1" */
#define EPNET_MAX_PACKET_SIZE 1232
#define EPNET_HEADER_SIZE 12
#ifndef EPNET_MAX_PAYLOAD
#define EPNET_MAX_PAYLOAD (EPNET_MAX_PACKET_SIZE - EPNET_HEADER_SIZE)
#endif
#ifndef EPNET_RELIABLE_MAX_DATA
#define EPNET_RELIABLE_MAX_DATA 256
#endif

#ifndef EPNET_MAX_CLIENTS
#define EPNET_MAX_CLIENTS 16
#endif
#ifndef EPNET_TIMEOUT_SEC
#define EPNET_TIMEOUT_SEC 10.0
#endif
#ifndef EPNET_HEARTBEAT_SEC
#define EPNET_HEARTBEAT_SEC 1.0
#endif
#ifndef EPNET_CONNECT_RETRY_SEC
#define EPNET_CONNECT_RETRY_SEC 0.5
#endif
#ifndef EPNET_CONNECT_MAX_TRIES
#define EPNET_CONNECT_MAX_TRIES 10
#endif
#ifndef EPNET_RELIABLE_RESEND_SEC
#define EPNET_RELIABLE_RESEND_SEC 0.1
#endif
#ifndef EPNET_DISCONNECT_SENDS
#define EPNET_DISCONNECT_SENDS 3
#endif
#ifndef EPNET_EVENT_QUEUE_SIZE
#define EPNET_EVENT_QUEUE_SIZE 64
#endif

/* Generic packet types (connection management + reserved) */
typedef enum {
    EPNET_PKT_CONNECT_REQ = 0x01,
    EPNET_PKT_CONNECT_ACK = 0x02,
    EPNET_PKT_CONNECT_DENY = 0x03,
    EPNET_PKT_DISCONNECT = 0x04,
    EPNET_PKT_HEARTBEAT = 0x05,
    EPNET_PKT_RELIABLE_MSG = 0x06,
    /* Application packet types start at 0x10 */
} epnet_packet_type_t;

/* Denial reasons */
typedef enum {
    EPNET_DENY_FULL = 0,
    EPNET_DENY_BANNED = 1,
    EPNET_DENY_VERSION_MISMATCH = 2,
} epnet_deny_reason_t;

/* Packet header (in-memory representation) */
typedef struct {
    uint16_t protocol_id;
    uint16_t sequence;
    uint16_t ack;
    uint32_t ack_bits;
    uint8_t type;
} epnet_packet_header_t;

/* Byte-order helpers */
void epnet_write_u16(uint8_t* buf, uint16_t val);
uint16_t epnet_read_u16(const uint8_t* buf);
void epnet_write_u32(uint8_t* buf, uint32_t val);
uint32_t epnet_read_u32(const uint8_t* buf);
void epnet_write_u64(uint8_t* buf, uint64_t val);
uint64_t epnet_read_u64(const uint8_t* buf);
void epnet_write_f32(uint8_t* buf, float val);
float epnet_read_f32(const uint8_t* buf);

/* Header serialization */
void epnet_packet_header_write(uint8_t* buf, const epnet_packet_header_t* hdr);
bool epnet_packet_header_read(
    const uint8_t* buf, size_t len, epnet_packet_header_t* out
);

/* Connection handshake payloads */
#define EPNET_CONNECT_REQ_SIZE 8
#define EPNET_CONNECT_ACK_SIZE 9
#define EPNET_CONNECT_DENY_SIZE 1
#define EPNET_DISCONNECT_SIZE 8

void epnet_write_connect_req(uint8_t* buf, uint64_t challenge);
uint64_t epnet_read_connect_req(const uint8_t* buf);

void epnet_write_connect_ack(
    uint8_t* buf, uint64_t challenge, uint8_t client_id
);
void epnet_read_connect_ack(
    const uint8_t* buf, uint64_t* challenge, uint8_t* client_id
);

/* Reliable message payload */
int epnet_write_reliable_msg(
    uint8_t* buf, uint16_t msg_id, const uint8_t* data, uint16_t data_len
);
int epnet_read_reliable_msg(
    const uint8_t* buf, int payload_len, uint16_t* msg_id, uint8_t* data,
    uint16_t* data_len
);

/* Sequence number comparison (handles wrap-around) */
static inline bool epnet_seq_greater(uint16_t a, uint16_t b)
{
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

#endif /* EPNET_PROTOCOL_H */
