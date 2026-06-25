/*
 * encoder.h - 视频编码接口（H.264 / FFmpeg）
 *
 * 输入原始帧 frame_t，输出编码包 packet_t。
 * 支持关键帧策略：可按需强制下一帧为 I 帧（新客户端/重连时用）。
 */
#ifndef ENCODE_ENCODER_H
#define ENCODE_ENCODER_H

#include "pipeline/types.h"

typedef struct {
    int width;    /* 输入帧宽 */
    int height;   /* 输入帧高 */
    int fps;      /* 帧率 */
    int bitrate;  /* 码率(bps) */
} encoder_opts_t;

typedef struct encoder encoder_t;

/* 创建编码器。成功 ERR_OK 并设置 *out。 */
int encoder_open(encoder_t **out, const encoder_opts_t *opts);

/*
 * 编码一帧。成功时 *out 为编码包（调用方 packet_free 释放）；
 * 编码器需要更多输入时 *out=NULL 并返回 ERR_AGAIN。
 */
int encoder_encode(encoder_t *enc, const frame_t *in, packet_t **out);

/* 请求下一帧编码为关键帧(I帧)。 */
void encoder_force_keyframe(encoder_t *enc);

/* 关闭并释放编码器。 */
void encoder_close(encoder_t *enc);

#endif /* ENCODE_ENCODER_H */
