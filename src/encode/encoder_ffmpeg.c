/*
 * encoder_ffmpeg.c - 基于 libavcodec 的 H.264 编码器
 *
 * 当前状态：M0 骨架。
 *   - encoder_open: 查找 H.264 编码器，验证 FFmpeg 链接正常（不真正初始化编码上下文）
 *   - encoder_encode: 暂未实现，返回 ERR_AGAIN
 *
 * M2 待实现：
 *   - avcodec_alloc_context3 + avcodec_open2 完整初始化（分辨率/帧率/码率/gop）
 *   - 用 libswscale 把捕获的像素格式转成 YUV420P
 *   - avcodec_send_frame / avcodec_receive_packet 编码
 *   - force_keyframe: 给下一帧设置 AV_PICTURE_TYPE_I
 */
#include "encode/encoder.h"
#include "common/error.h"
#include "common/log.h"

#include <stdlib.h>

#include <libavcodec/avcodec.h>

struct encoder {
    const AVCodec *codec;
    int force_key;   /* 下一帧是否强制 I 帧 */
    encoder_opts_t opts;
};

int encoder_open(encoder_t **out, const encoder_opts_t *opts)
{
    if (!out || !opts)
        return ERR_INVAL;

    encoder_t *enc = calloc(1, sizeof(*enc));
    if (!enc)
        return ERR_NOMEM;

    enc->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!enc->codec) {
        log_error("encoder: H.264 encoder not found in this FFmpeg build");
        free(enc);
        return ERR_ENCODE;
    }

    enc->opts = *opts;
    log_info("encoder: H.264 encoder found (%s), %dx%d@%dfps %dbps",
             enc->codec->name, opts->width, opts->height,
             opts->fps, opts->bitrate);

    /* TODO(M2): 在此分配并打开 AVCodecContext。 */

    *out = enc;
    return ERR_OK;
}

int encoder_encode(encoder_t *enc, const frame_t *in, packet_t **out)
{
    if (!enc || !in || !out)
        return ERR_INVAL;

    /* TODO(M2): 像素格式转换 + send_frame/receive_packet。 */
    (void)enc;
    (void)in;
    *out = NULL;
    return ERR_AGAIN;
}

void encoder_force_keyframe(encoder_t *enc)
{
    if (enc)
        enc->force_key = 1;
}

void encoder_close(encoder_t *enc)
{
    if (!enc)
        return;
    /* TODO(M2): 释放 AVCodecContext / swscale 上下文。 */
    free(enc);
}
