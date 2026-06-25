/*
 * ws.c - WebSocket 握手与帧编解码（自包含 SHA-1 + Base64，无外部依赖）
 */
#define _GNU_SOURCE   /* strcasestr */
#include "net/ws.h"
#include "common/error.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>  /* strncasecmp / strcasestr */

/* ===================== SHA-1（RFC 3174 精简实现） ===================== */

typedef struct {
    uint32_t state[5];
    uint64_t count;      /* 已处理位数 */
    uint8_t  buffer[64];
} sha1_ctx;

static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

static void sha1_transform(uint32_t state[5], const uint8_t buf[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)buf[i*4] << 24) | ((uint32_t)buf[i*4+1] << 16) |
               ((uint32_t)buf[i*4+2] << 8) | ((uint32_t)buf[i*4+3]);
    for (int i = 16; i < 80; ++i)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t tmp = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void sha1_init(sha1_ctx * c)
{
    c->state[0]=0x67452301; c->state[1]=0xEFCDAB89; c->state[2]=0x98BADCFE;
    c->state[3]=0x10325476; c->state[4]=0xC3D2E1F0; c->count = 0;
}

static void sha1_update(sha1_ctx *c, const uint8_t *data, size_t len)
{
    size_t idx = (size_t)((c->count >> 3) & 63);
    c->count += (uint64_t)len << 3;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(&c->buffer[idx], data, part);
        sha1_transform(c->state, c->buffer);
        for (i = part; i + 63 < len; i += 64)
            sha1_transform(c->state, &data[i]);
        idx = 0;
    }
    memcpy(&c->buffer[idx], &data[i], len - i);
}

static void sha1_final(sha1_ctx *c, uint8_t out[20])
{
    uint8_t finalcount[8];
    for (int i = 0; i < 8; ++i)
        finalcount[i] = (uint8_t)(c->count >> ((7 - i) * 8));
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (((c->count >> 3) & 63) != 56)
        sha1_update(c, &zero, 1);
    sha1_update(c, finalcount, 8);
    for (int i = 0; i < 20; ++i)
        out[i] = (uint8_t)(c->state[i>>2] >> ((3 - (i & 3)) * 8));
}

/* ===================== Base64 编码 ===================== */

static const char b64tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i+1] << 8;
        if (i + 2 < len) v |= in[i+2];
        out[o++] = b64tab[(v >> 18) & 0x3F];
        out[o++] = b64tab[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? b64tab[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? b64tab[v & 0x3F] : '=';
    }
    out[o] = '\0';
}

/* ===================== WebSocket 握手 ===================== */

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int ws_build_handshake(const char *request, char *out, size_t out_size)
{
    if (!request || !out)
        return ERR_INVAL;

    /* 找 Sec-WebSocket-Key: 这一行 */
    const char *key_hdr = strcasestr(request, "Sec-WebSocket-Key:");
    if (!key_hdr)
        return ERR_PROTOCOL;
    key_hdr += strlen("Sec-WebSocket-Key:");
    while (*key_hdr == ' ' || *key_hdr == '\t') key_hdr++;

    char key[128];
    size_t klen = 0;
    while (key_hdr[klen] && key_hdr[klen] != '\r' && key_hdr[klen] != '\n'
           && klen < sizeof(key) - 1) {
        key[klen] = key_hdr[klen];
        klen++;
    }
    key[klen] = '\0';

    /* accept = base64(sha1(key + GUID)) */
    char concat[256];
    int n = snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);
    if (n <= 0 || n >= (int)sizeof(concat))
        return ERR_PROTOCOL;

    uint8_t digest[20];
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)concat, (size_t)n);
    sha1_final(&ctx, digest);

    char accept[32];
    base64_encode(digest, 20, accept);

    int len = snprintf(out, out_size,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);
    if (len <= 0 || len >= (int)out_size)
        return ERR_IO;

    return len;
}

/* ===================== WebSocket 帧编码（服务端发，不掩码） ===================== */

int ws_encode_frame(const uint8_t *payload, size_t payload_len,
                    uint8_t opcode, uint8_t *out, size_t out_size)
{
    size_t header = 2;
    if (payload_len >= 126 && payload_len <= 0xFFFF)
        header += 2;
    else if (payload_len > 0xFFFF)
        header += 8;

    if (out_size < header + payload_len)
        return ERR_IO;

    out[0] = 0x80 | (opcode & 0x0F);   /* FIN=1 */

    if (payload_len < 126) {
        out[1] = (uint8_t)payload_len;
    } else if (payload_len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)(payload_len);
    } else {
        out[1] = 127;
        for (int i = 0; i < 8; ++i)
            out[2 + i] = (uint8_t)(payload_len >> ((7 - i) * 8));
    }

    if (payload_len > 0 && payload)
        memcpy(out + header, payload, payload_len);

    return (int)(header + payload_len);
}

/* ===================== WebSocket 帧解码（客户端发，需解掩码） ===================== */

int ws_decode_frame(uint8_t *buf, size_t buf_len, ws_frame_t *out)
{
    if (!buf || !out)
        return ERR_INVAL;
    if (buf_len < 2)
        return ERR_AGAIN;

    int fin    = (buf[0] & 0x80) ? 1 : 0;
    uint8_t op = buf[0] & 0x0F;
    int masked = (buf[1] & 0x80) ? 1 : 0;
    uint64_t plen = buf[1] & 0x7F;
    size_t pos = 2;

    if (plen == 126) {
        if (buf_len < pos + 2) return ERR_AGAIN;
        plen = ((uint64_t)buf[pos] << 8) | buf[pos+1];
        pos += 2;
    } else if (plen == 127) {
        if (buf_len < pos + 8) return ERR_AGAIN;
        plen = 0;
        for (int i = 0; i < 8; ++i)
            plen = (plen << 8) | buf[pos + i];
        pos += 8;
    }

    uint8_t mask[4] = {0,0,0,0};
    if (masked) {
        if (buf_len < pos + 4) return ERR_AGAIN;
        memcpy(mask, &buf[pos], 4);
        pos += 4;
    }

    if (buf_len < pos + plen)
        return ERR_AGAIN;  /* 负载还没收全 */

    /* 就地解掩码 */
    if (masked) {
        for (uint64_t i = 0; i < plen; ++i)
            buf[pos + i] ^= mask[i & 3];
    }

    out->opcode      = op;
    out->fin         = fin;
    out->payload     = buf + pos;
    out->payload_len = (size_t)plen;
    out->frame_len   = pos + (size_t)plen;
    return ERR_OK;
}
