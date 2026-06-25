/*
 * capture_x11.c - X11/XShm 桌面捕获后端
 *
 * 当前状态：M0 骨架。
 *   - capture_open: 连接 X11 显示，读取屏幕分辨率（验证 X11 链接正常）
 *   - capture_grab: 暂未实现真正截屏，返回 ERR_AGAIN（M1 用 XShm 填充）
 *
 * M1 待实现：用 XShmCreateImage + XShmGetImage 高效抓取桌面像素，
 *           按 scale_w/scale_h 缩放，按 fps 控制节奏。
 */
#include "capture/capture.h"
#include "common/error.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>

struct capture {
    Display *dpy;
    Window   root;
    int      screen;
    int      width;
    int      height;
    capture_opts_t opts;
};

int capture_open(capture_t **out, capture_backend_t backend,
                 const capture_opts_t *opts)
{
    if (!out || !opts)
        return ERR_INVAL;

    if (backend != CAPTURE_BACKEND_X11) {
        log_error("capture: backend %d not implemented yet", backend);
        return ERR_INVAL;
    }

    capture_t *cap = calloc(1, sizeof(*cap));
    if (!cap)
        return ERR_NOMEM;

    cap->dpy = XOpenDisplay(NULL); /* 使用 $DISPLAY 环境变量 */
    if (!cap->dpy) {
        log_error("capture: XOpenDisplay failed (is DISPLAY set? are you on X11?)");
        free(cap);
        return ERR_CAPTURE;
    }

    cap->screen = DefaultScreen(cap->dpy);
    cap->root   = RootWindow(cap->dpy, cap->screen);
    cap->width  = DisplayWidth(cap->dpy, cap->screen);
    cap->height = DisplayHeight(cap->dpy, cap->screen);
    cap->opts   = *opts;

    log_info("capture: X11 opened, desktop %dx%d", cap->width, cap->height);

    *out = cap;
    return ERR_OK;
}

int capture_grab(capture_t *cap, frame_t **out)
{
    if (!cap || !out)
        return ERR_INVAL;

    /* TODO(M1): 用 XShmGetImage 抓取一帧，填充 frame_t。 */
    (void)cap;
    *out = NULL;
    return ERR_AGAIN;
}

void capture_get_dimensions(capture_t *cap, int *width, int *height)
{
    if (!cap)
        return;
    if (width)
        *width = cap->width;
    if (height)
        *height = cap->height;
}

void capture_close(capture_t *cap)
{
    if (!cap)
        return;
    if (cap->dpy)
        XCloseDisplay(cap->dpy);
    free(cap);
}
