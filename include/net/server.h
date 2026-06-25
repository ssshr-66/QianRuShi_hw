/*
 * server.h - 网络服务端接口
 *
 * 职责：监听端口，用 epoll 处理并发连接，托管静态网页(web/)，
 *       并通过 WebSocket 向所有客户端推送编码帧。
 *
 * M3：epoll 多路复用 + 多客户端 + WebSocket 握手/帧 已实现。
 * M4：将由捕获/编码线程调用 server_broadcast() 推送 H.264 包。
 */
#ifndef NET_SERVER_H
#define NET_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct server server_t;

typedef struct {
    int         port;     /* 监听端口 */
    const char *web_root; /* 静态资源根目录 */
} server_opts_t;

/* 创建并绑定监听套接字。成功 ERR_OK 并设置 *out。 */
int server_create(server_t **out, const server_opts_t *opts);

/*
 * 运行服务事件循环，阻塞直到 *running 变为 false 或致命错误。
 * 返回 ERR_OK（正常停止）或负数。
 */
int server_run(server_t *srv, volatile bool *running);

/*
 * 向所有已完成握手的 WebSocket 客户端广播一段二进制数据（线程安全）。
 * 通常由编码线程调用，data 为一帧的应用层协议数据(帧头+H.264)。
 * 慢客户端按背压策略处理（缓冲满则丢弃该客户端的本帧）。
 * 返回成功发送的客户端数。
 */
int server_broadcast(server_t *srv, const uint8_t *data, size_t len);

/* 当前已连接的 WebSocket 客户端数。 */
int server_client_count(server_t *srv);

/* 关闭并释放服务。 */
void server_destroy(server_t *srv);

#endif /* NET_SERVER_H */
