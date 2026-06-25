/*
 * pipeline.h - 捕获→编码→广播 流水线
 *
 * 用两个线程把已有模块串成实时流水线：
 *   capture 线程: capture_grab -> frame 队列（满则丢最旧）
 *   encode  线程: 取 frame -> encoder_encode -> 帧协议打包 -> server_broadcast
 *
 * 线程在 pipeline_start 创建，pipeline_stop 优雅停止并 join。
 */
#ifndef PIPELINE_PIPELINE_H
#define PIPELINE_PIPELINE_H

#include "capture/capture.h"
#include "encode/encoder.h"
#include "net/server.h"

typedef struct pipeline pipeline_t;

typedef struct {
    capture_t *cap;
    encoder_t *enc;
    server_t  *srv;
    int        fps;
    int        queue_cap;   /* frame 队列容量，0 用默认 */
} pipeline_opts_t;

/* 创建并启动流水线线程。成功 ERR_OK 并设置 *out。 */
int pipeline_start(pipeline_t **out, const pipeline_opts_t *opts);

/* 停止流水线（通知线程退出并 join），随后可安全释放上游模块。 */
void pipeline_stop(pipeline_t *p);

#endif /* PIPELINE_PIPELINE_H */
