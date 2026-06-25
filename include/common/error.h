/*
 * error.h - 统一错误码定义
 *
 * 本项目约定：
 *   - 状态类函数返回 int：0 表示成功，负数表示错误（取 err_t 中的值）
 *   - 创建/分配类函数返回指针：失败返回 NULL
 *   - 错误向上层传播，由能决策的层（main / 网络循环）处理
 */
#ifndef COMMON_ERROR_H
#define COMMON_ERROR_H

typedef enum {
    ERR_OK        = 0,   /* 成功 */
    ERR_NOMEM     = -1,  /* 内存分配失败 */
    ERR_IO        = -2,  /* I/O / 系统调用失败 */
    ERR_CAPTURE   = -3,  /* 桌面捕获失败 */
    ERR_ENCODE    = -4,  /* 视频编码失败 */
    ERR_PROTOCOL  = -5,  /* 协议解析/打包失败 */
    ERR_CLOSED    = -6,  /* 对端/客户端关闭连接 */
    ERR_AGAIN     = -7,  /* 资源暂不可用，稍后重试（非阻塞 EAGAIN） */
    ERR_INVAL     = -8,  /* 非法参数 */
} err_t;

/* 将错误码转为可读字符串，便于打日志 */
const char *err_str(err_t e);

#endif /* COMMON_ERROR_H */
