/*
 * capture_x11.c - X11/XShm 桌面捕获后端（M1 实现）
 *
 * 用 MIT-SHM（X 共享内存扩展）高效抓取整个桌面：
 *   - open  : 连接 X11，创建一块共享内存 + XShmImage 绑定到根窗口尺寸
 *   - grab  : XShmGetImage 把当前桌面像素直接写入共享内存（零额外拷贝），
 *             再拷贝到 frame_t 输出；按 fps 控制抓取节奏
 *   - close : 卸载并释放共享内存、XImage、X 连接
 *
 * 像素格式：X11 32 位真彩通常为 BGRA（小端下内存序 B,G,R,A）。
 *   frame_t 不带格式字段，这里约定输出即为 32bpp BGRA，stride = width*4。
 *   M2 编码阶段用 libswscale 从 BGRA 转 YUV420P。
 *
 * 若服务器不支持 MIT-SHM（极少见，如远程 X），回退到普通 XGetImage。
 */
#include "capture/capture.h"
#include "common/error.h"
#include "common/log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <sys/ipc.h>

struct capture {
    Display *dpy;
    Window   root;
    int      screen;
    int      width;
    int      height;
    capture_opts_t opts;

    int      use_shm;          /* 1=用 XShm，0=回退 XGetImage */
    XImage  *image;            /* XShm 模式下的共享 XImage */
    XShmSegmentInfo shm;       /* 共享内存段信息 */

    uint64_t frame_interval_ns;/* 两帧最小间隔(ns)，由 fps 决定 */
    uint64_t last_grab_ns;     /* 上次抓取时间(ns) */
};

/* 取单调时钟纳秒 */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* 取墙钟毫秒（用于帧时间戳，给延迟测算用） */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* 创建 XShm 共享内存 XImage */
static int setup_shm(capture_t *cap)
{
    Visual *visual = DefaultVisual(cap->dpy, cap->screen);
    int depth = DefaultDepth(cap->dpy, cap->screen);

    cap->image = XShmCreateImage(cap->dpy, visual, (unsigned)depth, ZPixmap,
                                 NULL, &cap->shm,
                                 (unsigned)cap->width, (unsigned)cap->height);
    if (!cap->image) {
        log_warn("capture: XShmCreateImage failed, fallback to XGetImage");
        return ERR_CAPTURE;
    }

    /* 申请共享内存段 */
    cap->shm.shmid = shmget(IPC_PRIVATE,
                            (size_t)cap->image->bytes_per_line * cap->image->height,
                            IPC_CREAT | 0600);
    if (cap->shm.shmid < 0) {
        log_warn("capture: shmget failed, fallback to XGetImage");
        XDestroyImage(cap->image);
        cap->image = NULL;
        return ERR_CAPTURE;
    }

    cap->shm.shmaddr = shmat(cap->shm.shmid, NULL, 0);
    if (cap->shm.shmaddr == (char *)-1) {
        log_warn("capture: shmat failed, fallback to XGetImage");
        shmctl(cap->shm.shmid, IPC_RMID, NULL);
        XDestroyImage(cap->image);
        cap->image = NULL;
        return ERR_CAPTURE;
    }
    cap->image->data = cap->shm.shmaddr;
    cap->shm.readOnly = False;

    if (!XShmAttach(cap->dpy, &cap->shm)) {
        log_warn("capture: XShmAttach failed, fallback to XGetImage");
        shmdt(cap->shm.shmaddr);
        shmctl(cap->shm.shmid, IPC_RMID, NULL);
        XDestroyImage(cap->image);
        cap->image = NULL;
        return ERR_CAPTURE;
    }
    XSync(cap->dpy, False);

    /* attach 成功后即可标记删除，进程退出/detach 时内核回收 */
    shmctl(cap->shm.shmid, IPC_RMID, NULL);

    cap->use_shm = 1;
    log_info("capture: MIT-SHM enabled (depth=%d, bytes_per_line=%d)",
             depth, cap->image->bytes_per_line);
    return ERR_OK;
}

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

    cap->dpy = XOpenDisplay(NULL);
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

    int fps = opts->fps > 0 ? opts->fps : 30;
    cap->frame_interval_ns = 1000000000ULL / (uint64_t)fps;
    cap->last_grab_ns = 0;

    log_info("capture: X11 opened, desktop %dx%d, target %dfps",
             cap->width, cap->height, fps);

    /* 尝试启用 XShm；失败则用普通 XGetImage 回退 */
    if (XShmQueryExtension(cap->dpy)) {
        if (setup_shm(cap) != ERR_OK)
            cap->use_shm = 0;
    } else {
        log_warn("capture: server has no MIT-SHM, using XGetImage (slower)");
        cap->use_shm = 0;
    }

    *out = cap;
    return ERR_OK;
}

/* 把 XImage 的像素拷贝到新分配的 frame_t（BGRA, stride=width*4） */
static int image_to_frame(capture_t *cap, XImage *img, frame_t **out)
{
    int stride = cap->width * 4;   /* 输出统一 32bpp */
    frame_t *f = frame_alloc(cap->width, cap->height, stride);
    if (!f)
        return ERR_NOMEM;

    /* XImage 的 bytes_per_line 可能因对齐 >= width*4，需逐行拷贝 */
    int copy_bytes = cap->width * 4;
    if (img->bits_per_pixel == 32) {
        for (int y = 0; y < cap->height; ++y) {
            const uint8_t *src = (const uint8_t *)img->data + (size_t)y * img->bytes_per_line;
            uint8_t *dst = f->data + (size_t)y * stride;
            memcpy(dst, src, (size_t)copy_bytes);
        }
    } else {
        /* 非 32bpp 的情况较少见，简单置黑并告警一次 */
        memset(f->data, 0, f->size);
        log_warn("capture: unexpected bits_per_pixel=%d", img->bits_per_pixel);
    }

    f->timestamp = now_ms();
    *out = f;
    return ERR_OK;
}

int capture_grab(capture_t *cap, frame_t **out)
{
    if (!cap || !out)
        return ERR_INVAL;

    /* 帧率节奏控制：距上次不足一帧间隔则让调用方稍后再来 */
    uint64_t t = now_ns();
    if (cap->last_grab_ns != 0 &&
        (t - cap->last_grab_ns) < cap->frame_interval_ns) {
        return ERR_AGAIN;
    }

    if (cap->use_shm) {
        if (!XShmGetImage(cap->dpy, cap->root, cap->image, 0, 0, AllPlanes)) {
            log_error("capture: XShmGetImage failed");
            return ERR_CAPTURE;
        }
        int rc = image_to_frame(cap, cap->image, out);
        if (rc != ERR_OK)
            return rc;
    } else {
        XImage *img = XGetImage(cap->dpy, cap->root, 0, 0,
                                (unsigned)cap->width, (unsigned)cap->height,
                                AllPlanes, ZPixmap);
        if (!img) {
            log_error("capture: XGetImage failed");
            return ERR_CAPTURE;
        }
        int rc = image_to_frame(cap, img, out);
        XDestroyImage(img);
        if (rc != ERR_OK)
            return rc;
    }

    cap->last_grab_ns = t;
    return ERR_OK;
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

    if (cap->use_shm && cap->image) {
        XShmDetach(cap->dpy, &cap->shm);
        XDestroyImage(cap->image);   /* 不会 free data（data 是 shm） */
        if (cap->shm.shmaddr && cap->shm.shmaddr != (char *)-1)
            shmdt(cap->shm.shmaddr);
    }

    if (cap->dpy)
        XCloseDisplay(cap->dpy);
    free(cap);
}
