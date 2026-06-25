/*
 * ws.h - WebSocket 协议工具（握手 + 帧编解码）
 *
 * 只实现本项目需要的子集：
 *   - 服务端握手响应（Sec-WebSocket-Accept = base64(sha1(key + GUID))）
 *   - 服务端发送二进制帧的封装（不掩码，按 RFC6455 服务端规则）
 *   - 解析客户端发来的帧（必须解掩码）
 *
 * 内置精简 SHA-1 / Base64，不依赖 OpenSSL。
 */
#ifndef NET_WS_H
#define NET_WS_H

#include <stdint.h>
#include <stddef.h>

/* WebSocket 操作码 */
#define WS_OP_CONT   0x0
#define WS_OP_TEXT   0x1
#define WS_OP_BIN    0x2
#define WS_OP_CLOSE  0x8
#define WS_OP_PING   0x9
#define WS_OP_PONG   0xA

/*
 * 从 HTTP 升级请求里找到 Sec-WebSocket-Key，生成完整的 101 握手响应。
 * @param request   完整的 HTTP 请求文本
 * @param out        输出缓冲（写入握手响应文本）
 * @param out_size   out 容量
 * @return 响应长度(>0)；不是合法 WS 升级请求或缓冲不足返回负数(err_t)
 */
int ws_build_handshake(const char *request, char *out, size_t out_size);

/*
 * 把一段负载封装成 WebSocket 二进制帧（服务端→客户端，不加掩码）。
 * @param payload      负载数据
 * @param payload_len  负载长度
 * @param out          输出缓冲
 * @param out_size     out 容量
 * @return 写入的总字节数(帧头+负载)；缓冲不足返回负数
 */
int ws_encode_frame(const uint8_t *payload, size_t payload_len,
                    uint8_t opcode, uint8_t *out, size_t out_size);

/* 解析结果 */
typedef struct {
    uint8_t  opcode;       /* 操作码 */
    int      fin;          /* 是否结束帧 */
    const uint8_t *payload;/* 指向 buf 内已解掩码的负载（原地解码） */
    size_t   payload_len;  /* 负载长度 */
    size_t   frame_len;    /* 整帧消耗的字节数（用于推进缓冲区） */
} ws_frame_t;

/*
 * 解析客户端发来的一个 WebSocket 帧（会就地对 buf 解掩码）。
 * @param buf       可读写缓冲（解掩码会修改它）
 * @param buf_len   缓冲中可用字节数
 * @param out       解析结果
 * @return ERR_OK 解析出一帧；ERR_AGAIN 数据不完整需要更多字节；负数=错误
 */
int ws_decode_frame(uint8_t *buf, size_t buf_len, ws_frame_t *out);

#endif /* NET_WS_H */
