/*
 * frame_proto.h - 自定义二进制帧协议（服务端与 Web 客户端共享契约）
 *
 * 解决 TCP 粘包 / 帧边界问题：每个应用帧 = 固定长度头 + 变长负载。
 * 所有多字节字段使用网络字节序（大端）。
 *
 * !!! 重要：本头文件的布局必须与 web/js/protocol.js 严格一致 !!!
 *
 * 帧头布局（共 16 字节）：
 *   偏移  大小  字段
 *   0     1     type        (0=video, 1=keyframe, 2=control)
 *   1     1     flags       (保留)
 *   2     2     reserved    (对齐保留)
 *   4     8     timestamp   (毫秒，大端，用于延迟测算)
 *   12    4     payload_len (大端，负载字节数)
 *   16    N     payload
 */
#ifndef PROTOCOL_FRAME_PROTO_H
#define PROTOCOL_FRAME_PROTO_H

#include <stdint.h>
#include <stddef.h>

#define PROTO_HEADER_SIZE 16

typedef enum {
    PROTO_TYPE_VIDEO    = 0,  /* 普通视频帧(P帧) */
    PROTO_TYPE_KEYFRAME = 1,  /* 关键帧(I帧) */
    PROTO_TYPE_CONTROL  = 2,  /* 控制消息 */
} proto_type_t;

/*
 * 把一个帧头写入 buf（buf 至少 PROTO_HEADER_SIZE 字节）。
 * 返回写入的字节数（即 PROTO_HEADER_SIZE），参数非法返回负数。
 */
int proto_write_header(uint8_t *buf, size_t buf_size,
                       proto_type_t type, uint64_t timestamp,
                       uint32_t payload_len);

/* 已解析的帧头 */
typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint64_t timestamp;
    uint32_t payload_len;
} proto_header_t;

/*
 * 从 buf 解析帧头到 out。buf_size 必须 >= PROTO_HEADER_SIZE。
 * 返回 ERR_OK 或负数错误码。
 */
int proto_read_header(const uint8_t *buf, size_t buf_size, proto_header_t *out);

#endif /* PROTOCOL_FRAME_PROTO_H */
