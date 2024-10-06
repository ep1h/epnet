#include "epnet_protocol.h"

#include <string.h>

/* Byte-order helpers */

void epnet_write_u16(uint8_t* buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

uint16_t epnet_read_u16(const uint8_t* buf)
{
    return (uint16_t)((uint16_t)buf[0] << 8 | (uint16_t)buf[1]);
}

void epnet_write_u32(uint8_t* buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

uint32_t epnet_read_u32(const uint8_t* buf)
{
    return (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 |
           (uint32_t)buf[2] << 8 | (uint32_t)buf[3];
}

void epnet_write_u64(uint8_t* buf, uint64_t val)
{
    buf[0] = (uint8_t)(val >> 56);
    buf[1] = (uint8_t)(val >> 48);
    buf[2] = (uint8_t)(val >> 40);
    buf[3] = (uint8_t)(val >> 32);
    buf[4] = (uint8_t)(val >> 24);
    buf[5] = (uint8_t)(val >> 16);
    buf[6] = (uint8_t)(val >> 8);
    buf[7] = (uint8_t)(val);
}

uint64_t epnet_read_u64(const uint8_t* buf)
{
    return (uint64_t)buf[0] << 56 | (uint64_t)buf[1] << 48 |
           (uint64_t)buf[2] << 40 | (uint64_t)buf[3] << 32 |
           (uint64_t)buf[4] << 24 | (uint64_t)buf[5] << 16 |
           (uint64_t)buf[6] << 8 | (uint64_t)buf[7];
}

void epnet_write_f32(uint8_t* buf, float val)
{
    uint32_t bits;
    memcpy(&bits, &val, sizeof(bits));
    epnet_write_u32(buf, bits);
}

float epnet_read_f32(const uint8_t* buf)
{
    uint32_t bits = epnet_read_u32(buf);
    float val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

/* Header serialization */

void epnet_packet_header_write(uint8_t* buf, const epnet_packet_header_t* hdr)
{
    epnet_write_u16(buf + 0, hdr->protocol_id);
    epnet_write_u16(buf + 2, hdr->sequence);
    epnet_write_u16(buf + 4, hdr->ack);
    epnet_write_u32(buf + 6, hdr->ack_bits);
    buf[10] = hdr->type;
    buf[11] = 0; /* reserved */
}

bool epnet_packet_header_read(
    const uint8_t* buf, size_t len, epnet_packet_header_t* out
)
{
    if (len < EPNET_HEADER_SIZE) {
        return false;
    }

    out->protocol_id = epnet_read_u16(buf + 0);
    if (out->protocol_id != EPNET_PROTOCOL_ID) {
        return false;
    }

    out->sequence = epnet_read_u16(buf + 2);
    out->ack = epnet_read_u16(buf + 4);
    out->ack_bits = epnet_read_u32(buf + 6);
    out->type = buf[10];
    return true;
}

/* Payload serialization */

void epnet_write_connect_req(uint8_t* buf, uint64_t challenge)
{
    epnet_write_u64(buf, challenge);
}

uint64_t epnet_read_connect_req(const uint8_t* buf)
{
    return epnet_read_u64(buf);
}

void epnet_write_connect_ack(
    uint8_t* buf, uint64_t challenge, uint8_t client_id
)
{
    epnet_write_u64(buf, challenge);
    buf[8] = client_id;
}

void epnet_read_connect_ack(
    const uint8_t* buf, uint64_t* challenge, uint8_t* client_id
)
{
    *challenge = epnet_read_u64(buf);
    *client_id = buf[8];
}

int epnet_write_reliable_msg(
    uint8_t* buf, uint16_t msg_id, const uint8_t* data, uint16_t data_len
)
{
    epnet_write_u16(buf, msg_id);
    epnet_write_u16(buf + 2, data_len);
    memcpy(buf + 4, data, data_len);
    return 4 + (int)data_len;
}

int epnet_read_reliable_msg(
    const uint8_t* buf, int payload_len, uint16_t* msg_id, uint8_t* data,
    uint16_t* data_len
)
{
    if (payload_len < 4) {
        return -1;
    }

    *msg_id = epnet_read_u16(buf);
    *data_len = epnet_read_u16(buf + 2);

    if (*data_len > EPNET_RELIABLE_MAX_DATA ||
        payload_len < 4 + (int)*data_len) {
        return -1;
    }

    memcpy(data, buf + 4, *data_len);
    return 4 + (int)*data_len;
}
