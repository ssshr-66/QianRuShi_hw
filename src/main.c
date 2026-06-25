/*
 * main.c - 服务端入口
 *
 * 职责：解析命令行 -> 初始化各子系统 -> 启动服务事件循环 -> 优雅退出。
 *
 * 当前状态：M0 骨架。
 *   - 解析配置、初始化日志
 *   - 探测捕获后端(X11) 和编码器(H.264) 是否就绪（验证依赖链接）
 *   - 启动 HTTP 静态服务（浏览器可打开页面）
 *   - Ctrl+C 优雅退出
 *
 * M1~M4 待接入：创建队列 + 启动 capture/encode 线程 + WebSocket 推流。
 */
#include "common/config.h"
#include "common/error.h"
#include "common/log.h"
#include "capture/capture.h"
#include "encode/encoder.h"
#include "net/server.h"

#include "pipeline/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>

/* 运行标志，信号处理里置 false 以优雅退出 */
static volatile bool g_running = true;

static void on_signal(int sig)
{
    (void)sig;
    g_running = false;
}

/*
 * M1 自测：把一帧 BGRA 像素写成 PPM(P6, RGB) 图片，便于肉眼确认截屏成功。
 * 返回 0 成功。
 */
static int write_frame_ppm(const frame_t *f, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_error("grab-test: cannot open %s", path);
        return -1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", f->width, f->height);
    for (int y = 0; y < f->height; ++y) {
        const uint8_t *row = f->data + (size_t)y * f->stride;
        for (int x = 0; x < f->width; ++x) {
            const uint8_t *px = row + x * 4; /* BGRA */
            uint8_t rgb[3] = { px[2], px[1], px[0] }; /* R,G,B */
            fwrite(rgb, 1, 3, fp);
        }
    }
    fclose(fp);
    return 0;
}

/*
 * M1 自测流程：抓一帧存图后退出。验证 X11/XShm 截屏链路。
 */
static int run_grab_test(capture_t *cap)
{
    if (!cap) {
        log_error("grab-test: capture not available");
        return 1;
    }

    const char *out = "/tmp/grab_test.ppm";
    frame_t *f = NULL;
    int rc = ERR_AGAIN;

    /* 第一帧可能因 fps 节奏返回 AGAIN，循环抓到为止（最多重试若干次） */
    for (int i = 0; i < 200 && rc == ERR_AGAIN; ++i) {
        rc = capture_grab(cap, &f);
        if (rc == ERR_AGAIN) {
            struct timespec ts = { 0, 5 * 1000000L }; /* 5ms */
            nanosleep(&ts, NULL);
        }
    }

    if (rc != ERR_OK || !f) {
        log_error("grab-test: capture_grab failed: %s", err_str(rc));
        return 1;
    }

    log_info("grab-test: captured frame %dx%d, %zu bytes, ts=%llu",
             f->width, f->height, f->size, (unsigned long long)f->timestamp);

    if (write_frame_ppm(f, out) == 0)
        log_info("grab-test: wrote %s  (open it to verify the screenshot)", out);

    frame_free(f);
    return 0;
}

int main(int argc, char **argv)
{
    /* 1. 解析配置 */
    server_config_t cfg;
    config_init_defaults(&cfg);
    int rc = config_parse_args(&cfg, argc, argv);
    if (rc == 1)
        return 0;          /* -h/--help 已打印用法 */
    if (rc != ERR_OK)
        return 1;

    /* 2. 日志级别 */
    log_set_level(cfg.verbose ? LOG_DEBUG : LOG_INFO);
    log_info("=== LAN Desktop Streaming Server (M0 skeleton) ===");
    config_print(&cfg);

    /* 3. 安装信号处理（Ctrl+C / kill 优雅退出） */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);   /* 客户端断开时写操作不致命 */

    /* 4. 探测捕获后端（验证 X11 链接与显示可用） */
    capture_t *cap = NULL;
    capture_opts_t cap_opts = {
        .fps = cfg.fps, .scale_w = cfg.scale_w, .scale_h = cfg.scale_h
    };
    rc = capture_open(&cap, CAPTURE_BACKEND_X11, &cap_opts);
    if (rc != ERR_OK) {
        log_warn("capture init failed (%s) - continuing without capture (M0)",
                 err_str(rc));
    } else {
        int w = 0, h = 0;
        capture_get_dimensions(cap, &w, &h);
        log_info("capture ready: desktop %dx%d", w, h);
    }

    /* 4.5 M1 自测模式：抓一帧存图后退出 */
    if (cfg.grab_test) {
        int test_rc = run_grab_test(cap);
        capture_close(cap);
        return test_rc;
    }

    /* 5. 探测编码器（验证 FFmpeg H.264 可用） */
    encoder_t *enc = NULL;
    if (cap) {
        int w = 0, h = 0;
        capture_get_dimensions(cap, &w, &h);
        encoder_opts_t enc_opts = {
            .width = (cfg.scale_w > 0 ? cfg.scale_w : w),
            .height = (cfg.scale_h > 0 ? cfg.scale_h : h),
            .fps = cfg.fps, .bitrate = cfg.bitrate
        };
        rc = encoder_open(&enc, &enc_opts);
        if (rc != ERR_OK)
            log_warn("encoder init failed (%s) - continuing (M0)", err_str(rc));
        else
            log_info("encoder ready");
    }

    /* 6. 启动网络服务（HTTP 静态托管）。M3/M4 会接入 WebSocket 推流。 */
    server_t *srv = NULL;
    server_opts_t srv_opts = { .port = cfg.port, .web_root = cfg.web_root };
    rc = server_create(&srv, &srv_opts);
    if (rc != ERR_OK) {
        log_error("server create failed: %s", err_str(rc));
        encoder_close(enc);
        capture_close(cap);
        return 1;
    }

    log_info("open http://<this-machine-ip>:%d/ in a LAN browser. Ctrl+C to stop.",
             cfg.port);

    /* 7. 进入事件循环（阻塞直到收到退出信号） */
    rc = server_run(srv, &g_running);
    if (rc != ERR_OK)
        log_error("server run ended with error: %s", err_str(rc));

    /* 8. 优雅清理（与创建顺序相反） */
    log_info("shutting down...");
    server_destroy(srv);
    encoder_close(enc);
    capture_close(cap);
    log_info("bye");

    return 0;
}
