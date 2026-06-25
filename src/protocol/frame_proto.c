#include "protocol/frame_proto.h"
#include "common/error.h"

#include <string.h>

/* 大端写入辅助 */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (56 - i * 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

int proto_write_header(uint8_t *buf, size_t buf_size,
                       proto_type_t type, uint64_t timestamp,
                       uint32_t payload_len)
{
    if (!buf || buf_size < PROTO_HEADER_SIZE)
        return ERR_INVAL;

    buf[0] = (uint8_t)type;   /* type */
    buf[1] = 0;               /* flags */
    put_u16(buf + 2, 0);      /* reserved */
    put_u64(buf + 4, timestamp);
    put_u32(buf + 12, payload_len);

    return PROTO_HEADER_SIZE;
}

int proto_read_header(const uint8_t *buf, size_t buf_size, proto_header_t *out)
{
    if (!buf || !out || buf_size < PROTO_HEADER_SIZE)
        return ERR_INVAL;

    out->type        = buf[0];
    out->flags       = buf[1];
    out->timestamp   = get_u64(buf + 4);
    out->payload_len = get_u32(buf + 12);

    return ERR_OK;
}
