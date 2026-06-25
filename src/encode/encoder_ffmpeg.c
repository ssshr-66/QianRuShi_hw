/*
 * encoder_ffmpeg.c - 基于 libavcodec 的 H.264 编码器（M2 实现）
 *
 * 流程：
 *   open   : 查找 libx264 -> 配置 AVCodecContext(分辨率/帧率/码率/GOP/低延迟)
 *            -> 打开编码器 -> 分配 YUV420P 的 AVFrame -> 建立 BGRA->YUV420P 的
 *            swscale 转换上下文
 *   encode : 把输入 BGRA 帧用 swscale 转成 YUV420P，送入编码器，取出 H.264 包，
 *            填入 packet_t（含是否关键帧、时间戳）
 *   force_keyframe : 让下一帧编码为 IDR(I 帧)，用于新客户端/重连快速起播
 *   close  : 逆序释放所有资源
 *
 * 低延迟要点：
 *   - tune=zerolatency，关闭 B 帧（max_b_frames=0），避免编码缓冲带来的延迟
 *   - 单帧进、单帧出，不积压
 */
#include "encode/encoder.h"
#include "common/error.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

struct encoder {
    const AVCodec  *codec;
    AVCodecContext *ctx;
    AVFrame        *yuv;      /* 复用的 YUV420P 帧 */
    AVPacket       *pkt;      /* 复用的输出包 */
    struct SwsContext *sws;   /* BGRA -> YUV420P 转换 */
    int64_t         pts;      /* 递增的帧序号(pts) */
    int             force_key;/* 下一帧强制 I 帧 */
    encoder_opts_t  opts;
};

int encoder_open(encoder_t **out, const encoder_opts_t *opts)
{
    if (!out || !opts)
        return ERR_INVAL;
    if (opts->width <= 0 || opts->height <= 0) {
        log_error("encoder: invalid dimensions %dx%d", opts->width, opts->height);
        return ERR_INVAL;
    }

    err_t rc = ERR_OK;
    encoder_t *enc = calloc(1, sizeof(*enc));
    if (!enc)
        return ERR_NOMEM;
    enc->opts = *opts;

    enc->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!enc->codec) {
        log_error("encoder: H.264 encoder not found");
        rc = ERR_ENCODE; goto fail;
    }

    enc->ctx = avcodec_alloc_context3(enc->codec);
    if (!enc->ctx) { rc = ERR_NOMEM; goto fail; }

    /* H.264 编码偶数尺寸更安全 */
    enc->ctx->width      = opts->width  & ~1;
    enc->ctx->height     = opts->height & ~1;
    enc->ctx->time_base  = (AVRational){1, opts->fps > 0 ? opts->fps : 30};
    enc->ctx->framerate  = (AVRational){opts->fps > 0 ? opts->fps : 30, 1};
    enc->ctx->pix_fmt    = AV_PIX_FMT_YUV420P;
    enc->ctx->bit_rate   = opts->bitrate > 0 ? opts->bitrate : 4000000;
    enc->ctx->gop_size   = (opts->fps > 0 ? opts->fps : 30) * 2; /* 约2秒一个I帧 */
    enc->ctx->max_b_frames = 0;  /* 关 B 帧：低延迟 */

    /* libx264 低延迟调优 */
    av_opt_set(enc->ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(enc->ctx->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(enc->ctx, enc->codec, NULL) < 0) {
        log_error("encoder: avcodec_open2 failed");
        rc = ERR_ENCODE; goto fail;
    }

    /* 复用的 YUV 帧 */
    enc->yuv = av_frame_alloc();
    if (!enc->yuv) { rc = ERR_NOMEM; goto fail; }
    enc->yuv->format = AV_PIX_FMT_YUV420P;
    enc->yuv->width  = enc->ctx->width;
    enc->yuv->height = enc->ctx->height;
    if (av_frame_get_buffer(enc->yuv, 32) < 0) { rc = ERR_NOMEM; goto fail; }

    enc->pkt = av_packet_alloc();
    if (!enc->pkt) { rc = ERR_NOMEM; goto fail; }

    /* BGRA(输入捕获格式) -> YUV420P 转换器 */
    enc->sws = sws_getContext(
        enc->ctx->width, enc->ctx->height, AV_PIX_FMT_BGRA,
        enc->ctx->width, enc->ctx->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!enc->sws) {
        log_error("encoder: sws_getContext failed");
        rc = ERR_ENCODE; goto fail;
    }

    enc->pts = 0;
    log_info("encoder: H.264 ready (%s) %dx%d@%dfps %dbps gop=%d",
             enc->codec->name, enc->ctx->width, enc->ctx->height,
             enc->ctx->framerate.num, enc->ctx->bit_rate, enc->ctx->gop_size);

    *out = enc;
    return ERR_OK;

fail:
    encoder_close(enc);
    *out = NULL;
    return rc;
}

int encoder_encode(encoder_t *enc, const frame_t *in, packet_t **out)
{
    if (!enc || !in || !out)
        return ERR_INVAL;
    *out = NULL;

    /* 1. 输入 BGRA -> YUV420P */
    if (av_frame_make_writable(enc->yuv) < 0)
        return ERR_ENCODE;

    const uint8_t *src_slices[4] = { in->data, NULL, NULL, NULL };
    int src_stride[4] = { in->stride, 0, 0, 0 };
    sws_scale(enc->sws, src_slices, src_stride, 0, enc->ctx->height,
              enc->yuv->data, enc->yuv->linesize);

    enc->yuv->pts = enc->pts++;

    /* 需要强制关键帧？设 pict_type=I 即可让编码器产出 IDR 帧 */
    if (enc->force_key) {
        enc->yuv->pict_type = AV_PICTURE_TYPE_I;
        enc->force_key = 0;
    } else {
        enc->yuv->pict_type = AV_PICTURE_TYPE_NONE; /* 让编码器自行决定 */
    }

    /* 2. 送入编码器 */
    int ret = avcodec_send_frame(enc->ctx, enc->yuv);
    if (ret < 0) {
        log_error("encoder: send_frame failed (%d)", ret);
        return ERR_ENCODE;
    }

    /* 3. 取出编码包（zerolatency 下通常一进一出） */
    ret = avcodec_receive_packet(enc->ctx, enc->pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return ERR_AGAIN;  /* 暂无输出，调用方下次再来 */
    }
    if (ret < 0) {
        log_error("encoder: receive_packet failed (%d)", ret);
        return ERR_ENCODE;
    }

    /* 4. 拷贝到 packet_t 输出 */
    packet_t *p = packet_alloc((size_t)enc->pkt->size);
    if (!p) {
        av_packet_unref(enc->pkt);
        return ERR_NOMEM;
    }
    memcpy(p->data, enc->pkt->data, (size_t)enc->pkt->size);
    p->size        = (size_t)enc->pkt->size;
    p->timestamp   = in->timestamp;
    p->is_keyframe = (enc->pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;

    av_packet_unref(enc->pkt);

    *out = p;
    return ERR_OK;
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
    if (enc->sws)
        sws_freeContext(enc->sws);
    if (enc->pkt)
        av_packet_free(&enc->pkt);
    if (enc->yuv)
        av_frame_free(&enc->yuv);
    if (enc->ctx)
        avcodec_free_context(&enc->ctx);
    free(enc);
}
