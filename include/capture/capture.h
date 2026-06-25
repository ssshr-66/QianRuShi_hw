/*
 * capture.h - 桌面捕获接口（后端无关）
 *
 * 用接口隔离具体后端（X11/XShm、未来的 Wayland/PipeWire），
 * 上层只依赖这个接口，不关心底层怎么截屏。
 *
 * 典型用法：
 *   capture_t *cap;
 *   capture_open(&cap, CAPTURE_BACKEND_X11, &opts);
 *   while (running) {
 *       frame_t *f;
 *       capture_grab(cap, &f);   // 取一帧
 *       ... 交给编码 ...
 *   }
 *   capture_close(cap);
 */
#ifndef CAPTURE_CAPTURE_H
#define CAPTURE_CAPTURE_H

#include "pipeline/types.h"

typedef enum {
    CAPTURE_BACKEND_X11 = 0,   /* X11 / XShm（M1 先实现这个） */
    CAPTURE_BACKEND_WAYLAND,   /* 预留：Wayland/PipeWire */
} capture_backend_t;

typedef struct {
    int fps;        /* 目标帧率 */
    int scale_w;    /* 输出宽，0=原始 */
    int scale_h;    /* 输出高，0=原始 */
} capture_opts_t;

/* 不透明句柄 */
typedef struct capture capture_t;

/* 打开捕获后端。成功返回 ERR_OK 并设置 *out；失败返回负数。 */
int capture_open(capture_t **out, capture_backend_t backend,
                 const capture_opts_t *opts);

/*
 * 抓取一帧到 *out（调用方负责 frame_free 释放）。
 * 成功 ERR_OK；暂无新帧 ERR_AGAIN；出错负数。
 */
int capture_grab(capture_t *cap, frame_t **out);

/* 获取桌面原始分辨率（在 open 之后可用） */
void capture_get_dimensions(capture_t *cap, int *width, int *height);

/* 关闭并释放捕获后端。 */
void capture_close(capture_t *cap);

#endif /* CAPTURE_CAPTURE_H */
