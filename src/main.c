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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

/* 运行标志，信号处理里置 false 以优雅退出 */
static volatile bool g_running = true;

static void on_signal(int sig)
{
    (void)sig;
    g_running = false;
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
