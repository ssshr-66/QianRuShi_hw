/*
 * server.h - 网络服务端接口
 *
 * 职责：监听端口，用 epoll 处理并发连接，托管静态网页(web/)，
 *       并（M3/M4）通过 WebSocket 向客户端推送编码帧。
 *
 * 当前状态：M0 提供最小 HTTP 静态文件服务，验证网络链路。
 *           epoll 多路复用 + WebSocket 推流在 M3/M4 实现。
 */
#ifndef NET_SERVER_H
#define NET_SERVER_H

#include <stdbool.h>

typedef struct server server_t;

typedef struct {
    int         port;     /* 监听端口 */
    const char *web_root; /* 静态资源根目录 */
} server_opts_t;

/* 创建并绑定监听套接字。成功 ERR_OK 并设置 *out。 */
int server_create(server_t **out, const server_opts_t *opts);

/*
 * 运行服务事件循环，阻塞直到 server_stop 被调用或致命错误。
 * 返回 ERR_OK（正常停止）或负数。
 */
int server_run(server_t *srv, volatile bool *running);

/* 关闭并释放服务。 */
void server_destroy(server_t *srv);

#endif /* NET_SERVER_H */
