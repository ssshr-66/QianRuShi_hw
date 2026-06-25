/*
 * pipeline.c - 捕获→编码→广播 流水线（M4）
 *
 * 线程模型：
 *   capture 线程：capture_grab() -> frame 队列（满则丢最旧，保证实时）
 *   encode  线程：队列取 frame -> encoder_encode() -> 帧协议打包 -> server_broadcast()
 *
 * 起播策略：
 *   - 编码器默认约 2 秒一个关键帧(gop)
 *   - 另外每 PERIODIC_KEY_MS 主动强制一次关键帧，让新连接/重连的客户端尽快出画面
 *
 * 内存/资源：队列以 frame_free 作为元素释放回调；线程退出后由 stop 统一回收。
 */
#include "pipeline/pipeline.h"
#include "pipeline/types.h"
#include "pipeline/queue.h"
#include "protocol/frame_proto.h"
#include "common/error.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define DEFAULT_QUEUE_CAP   8
#define PERIODIC_KEY_MS     1000   /* 每秒强制一个关键帧，利于新客户端起播 */

struct pipeline {
    pipeline_opts_t opts;
    queue_t  *frames;          /* capture -> encode 的 frame 队列 */
    pthread_t cap_thread;
    pthread_t enc_thread;
    volatile bool running;

    /* 统计 */
    uint64_t cap_count;
    uint64_t enc_count;
    uint64_t drop_count;
};

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* ---------- 捕获线程 ---------- */
static void *capture_loop(void *arg)
{
    pipeline_t *p = arg;
    log_info("pipeline: capture thread started");

    while (p->running) {
        frame_t *f = NULL;
        int rc = capture_grab(p->opts.cap, &f);
        if (rc == ERR_AGAIN) {
            /* 未到下一帧时间，小睡一会再来 */
            struct timespec ts = { 0, 2 * 1000000L };
            nanosleep(&ts, NULL);
            continue;
        }
        if (rc != ERR_OK || !f) {
            log_warn("pipeline: capture_grab error: %s", err_str(rc));
            struct timespec ts = { 0, 10 * 1000000L };
            nanosleep(&ts, NULL);
            continue;
        }

        p->cap_count++;
        int dropped = queue_push_drop_oldest(p->frames, f);
        if (dropped > 0) {
            p->drop_count += (uint64_t)dropped;
            log_debug("pipeline: frame queue full, dropped oldest (total=%llu)",
                      (unsigned long long)p->drop_count);
        }
    }

    log_info("pipeline: capture thread exiting (captured=%llu, dropped=%llu)",
             (unsigned long long)p->cap_count,
             (unsigned long long)p->drop_count);
    return NULL;
}

/* ---------- 编码线程 ---------- */
static void *encode_loop(void *arg)
{
    pipeline_t *p = arg;
    log_info("pipeline: encode thread started");

    uint64_t last_key_ms = 0;
    int last_client_count = 0;

    /* 发送缓冲：帧协议头 + H.264 负载 */
    uint8_t *sendbuf = NULL;
    size_t   sendcap = 0;

    while (p->running) {
        void *item = NULL;
        int rc = queue_pop(p->frames, &item, 200); /* 最多等 200ms */
        if (rc == ERR_AGAIN)
            continue;           /* 超时，回去检查 running */
        if (rc == ERR_CLOSED)
            break;
        if (rc != ERR_OK || !item)
            continue;

        frame_t *f = item;

        /* 起播策略：周期性 或 有新客户端接入时，强制关键帧 */
        uint64_t t = now_ms();
        int clients = server_client_count(p->opts.srv);
        bool force_key = false;
        if (clients > last_client_count)
            force_key = true;                       /* 新客户端 */
        if (t - last_key_ms >= PERIODIC_KEY_MS)
            force_key = true;                       /* 周期性 */
        last_client_count = clients;

        if (force_key) {
            encoder_force_keyframe(p->opts.enc);
            last_key_ms = t;
        }

        /* 编码 */
        packet_t *pkt = NULL;
        rc = encoder_encode(p->opts.enc, f, &pkt);
        frame_free(f);

        if (rc == ERR_AGAIN)
            continue;
        if (rc != ERR_OK || !pkt) {
            log_warn("pipeline: encode error: %s", err_str(rc));
            continue;
        }

        p->enc_count++;

        /* 没有客户端就不发，省带宽 */
        if (clients > 0) {
            size_t need = PROTO_HEADER_SIZE + pkt->size;
            if (need > sendcap) {
                uint8_t *nb = realloc(sendbuf, need);
                if (!nb) {
                    log_error("pipeline: oom for sendbuf");
                    packet_free(pkt);
                    continue;
                }
                sendbuf = nb;
                sendcap = need;
            }

            proto_type_t type = pkt->is_keyframe ? PROTO_TYPE_KEYFRAME
                                                 : PROTO_TYPE_VIDEO;
            proto_write_header(sendbuf, sendcap, type, pkt->timestamp,
                               (uint32_t)pkt->size);
            memcpy(sendbuf + PROTO_HEADER_SIZE, pkt->data, pkt->size);

            int sent = server_broadcast(p->opts.srv, sendbuf, need);
            log_debug("pipeline: frame -> %zu bytes key=%d sent=%d",
                      pkt->size, pkt->is_keyframe, sent);
        }

        packet_free(pkt);
    }

    free(sendbuf);
    log_info("pipeline: encode thread exiting (encoded=%llu)",
             (unsigned long long)p->enc_count);
    return NULL;
}

int pipeline_start(pipeline_t **out, const pipeline_opts_t *opts)
{
    if (!out || !opts || !opts->cap || !opts->enc || !opts->srv)
        return ERR_INVAL;

    pipeline_t *p = calloc(1, sizeof(*p));
    if (!p)
        return ERR_NOMEM;
    p->opts = *opts;

    int cap = opts->queue_cap > 0 ? opts->queue_cap : DEFAULT_QUEUE_CAP;
    p->frames = queue_create((size_t)cap, (queue_free_fn)frame_free);
    if (!p->frames) {
        free(p);
        return ERR_NOMEM;
    }

    p->running = true;

    if (pthread_create(&p->cap_thread, NULL, capture_loop, p) != 0) {
        log_error("pipeline: failed to start capture thread");
        queue_destroy(p->frames);
        free(p);
        return ERR_IO;
    }
    if (pthread_create(&p->enc_thread, NULL, encode_loop, p) != 0) {
        log_error("pipeline: failed to start encode thread");
        p->running = false;
        pthread_join(p->cap_thread, NULL);
        queue_destroy(p->frames);
        free(p);
        return ERR_IO;
    }

    log_info("pipeline: started (queue_cap=%d)", cap);
    *out = p;
    return ERR_OK;
}

void pipeline_stop(pipeline_t *p)
{
    if (!p)
        return;

    log_info("pipeline: stopping...");
    p->running = false;
    queue_close(p->frames);   /* 唤醒阻塞在 pop 的编码线程 */

    pthread_join(p->cap_thread, NULL);
    pthread_join(p->enc_thread, NULL);

    queue_destroy(p->frames); /* 释放残留 frame */
    log_info("pipeline: stopped (captured=%llu encoded=%llu dropped=%llu)",
             (unsigned long long)p->cap_count,
             (unsigned long long)p->enc_count,
             (unsigned long long)p->drop_count);
    free(p);
}
