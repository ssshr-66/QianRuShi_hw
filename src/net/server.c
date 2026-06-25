/*
 * server.c - epoll 事件驱动的网络服务（M3）
 *
 * 功能：
 *   - 非阻塞监听 + epoll 多路复用，单线程管理多个客户端
 *   - 每个客户端一个状态机：HTTP 请求 -> 静态文件 或 WebSocket 升级
 *   - WebSocket 握手 + 帧编解码（见 ws.c）
 *   - server_broadcast()：把一帧数据推给所有 WS 客户端（线程安全，供编码线程调用）
 *   - 慢客户端背压：每客户端有发送缓冲，满则丢弃本帧，不阻塞其他客户端
 *
 * 并发说明：epoll 循环在 server_run 所在线程；broadcast 可能被其他线程调用，
 *          因此对客户端列表与发送缓冲用一把互斥锁保护。
 */
#define _GNU_SOURCE   /* strcasestr */
#include "net/server.h"
#include "net/ws.h"
#include "common/error.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define MAX_EVENTS    64
#define MAX_CLIENTS   64
#define RECV_BUF      8192
#define SEND_BUF_CAP  (4 * 1024 * 1024)  /* 每客户端发送缓冲上限 4MB */

typedef enum {
    CST_HTTP = 0,   /* 还在收 HTTP 请求 */
    CST_WS,         /* 已升级为 WebSocket */
    CST_CLOSING,    /* 待关闭 */
} client_state_t;

typedef struct {
    int            fd;
    client_state_t state;
    char           ip[INET_ADDRSTRLEN];

    /* 接收缓冲（HTTP 请求 / WS 入站帧） */
    uint8_t  rbuf[RECV_BUF];
    size_t   rlen;

    /* 发送缓冲（环形不必要，简单线性 + 已发偏移） */
    uint8_t *sbuf;
    size_t   scap;
    size_t   slen;     /* 缓冲中待发字节 */
    size_t   soff;     /* 已发偏移 */
} client_t;

struct server {
    int      listen_fd;
    int      epoll_fd;
    char     web_root[512];

    client_t *clients[MAX_CLIENTS];
    int       nclients;

    pthread_mutex_t lock;   /* 保护 clients[] 与各客户端发送缓冲 */
};

/* ---------- 工具 ---------- */

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return ERR_IO;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return ERR_IO;
    return ERR_OK;
}

static void epoll_mod(int epfd, int fd, uint32_t events, void *ptr)
{
    struct epoll_event ev;
    ev.events   = events;
    ev.data.ptr = ptr;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

/* ---------- 客户端生命周期 ---------- */

static client_t *client_new(int fd, const char *ip)
{
    client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = fd;
    c->state = CST_HTTP;
    snprintf(c->ip, sizeof(c->ip), "%s", ip ? ip : "?");
    c->scap = 64 * 1024;
    c->sbuf = malloc(c->scap);
    if (!c->sbuf) { free(c); return NULL; }
    return c;
}

static void client_free(client_t *c)
{
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    free(c->sbuf);
    free(c);
}

/* 把数据追加到客户端发送缓冲（需持锁）。容量不足则按需扩容到上限。 */
static int client_queue(client_t *c, const uint8_t *data, size_t len)
{
    /* 压实已发送部分 */
    if (c->soff > 0) {
        memmove(c->sbuf, c->sbuf + c->soff, c->slen - c->soff);
        c->slen -= c->soff;
        c->soff = 0;
    }

    if (c->slen + len > c->scap) {
        size_t need = c->slen + len;
        if (need > SEND_BUF_CAP)
            return ERR_AGAIN;  /* 背压：缓冲满，丢弃本帧 */
        size_t ncap = c->scap * 2;
        while (ncap < need) ncap *= 2;
        if (ncap > SEND_BUF_CAP) ncap = SEND_BUF_CAP;
        uint8_t *nb = realloc(c->sbuf, ncap);
        if (!nb) return ERR_NOMEM;
        c->sbuf = nb;
        c->scap = ncap;
    }

    memcpy(c->sbuf + c->slen, data, len);
    c->slen += len;
    return ERR_OK;
}

/* 尝试把发送缓冲写出，返回是否还有剩余待发 */
static int client_flush(client_t *c)
{
    while (c->soff < c->slen) {
        ssize_t n = send(c->fd, c->sbuf + c->soff, c->slen - c->soff, MSG_NOSIGNAL);
        if (n > 0) {
            c->soff += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 1; /* 还有剩余，等下次可写 */
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            c->state = CST_CLOSING;
            return 0;
        }
    }
    /* 全部发完，复位 */
    c->soff = 0;
    c->slen = 0;
    return 0;
}

/* ---------- HTTP 静态文件 ---------- */

static const char *content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html; charset=utf-8";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".css"))  return "text/css";
    return "application/octet-stream";
}

/* 把静态文件内容排入发送缓冲 */
static void serve_static(server_t *srv, client_t *c, const char *path)
{
    char relpath[512];
    if (strcmp(path, "/") == 0)
        snprintf(relpath, sizeof(relpath), "index.html");
    else
        snprintf(relpath, sizeof(relpath), "%s", path + 1);

    if (strstr(relpath, "..")) {
        const char *r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        client_queue(c, (const uint8_t *)r, strlen(r));
        c->state = CST_CLOSING;
        return;
    }

    char fullpath[1100];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", srv->web_root, relpath);

    FILE *fp = fopen(fullpath, "rb");
    if (!fp) {
        const char *r = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n"
                        "Content-Length: 9\r\n\r\nNot Found";
        client_queue(c, (const uint8_t *)r, strlen(r));
        c->state = CST_CLOSING;
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
        "Connection: close\r\n\r\n", content_type(relpath), fsize);
    client_queue(c, (const uint8_t *)header, (size_t)hlen);

    char fbuf[8192];
    size_t rd;
    while ((rd = fread(fbuf, 1, sizeof(fbuf), fp)) > 0)
        client_queue(c, (const uint8_t *)fbuf, rd);
    fclose(fp);

    c->state = CST_CLOSING;  /* HTTP 静态用短连接，发完即关 */
    log_debug("served %s (%ld bytes) to %s", relpath, fsize, c->ip);
}

/* ---------- 处理 HTTP 请求（含 WS 升级判定） ---------- */

static void handle_http(server_t *srv, client_t *c)
{
    c->rbuf[c->rlen] = '\0';
    /* 请求头是否接收完整（出现 \r\n\r\n） */
    if (!strstr((char *)c->rbuf, "\r\n\r\n"))
        return; /* 还没收全，继续等 */

    /* 是否为 WebSocket 升级请求 */
    int is_ws = strcasestr((char *)c->rbuf, "Upgrade: websocket") != NULL;

    if (is_ws) {
        char resp[512];
        int rlen = ws_build_handshake((char *)c->rbuf, resp, sizeof(resp));
        if (rlen <= 0) {
            const char *bad = "HTTP/1.1 400 Bad Request\r\n\r\n";
            client_queue(c, (const uint8_t *)bad, strlen(bad));
            c->state = CST_CLOSING;
            return;
        }
        client_queue(c, (const uint8_t *)resp, (size_t)rlen);
        c->state = CST_WS;
        c->rlen = 0;
        log_info("client %s upgraded to WebSocket (fd=%d)", c->ip, c->fd);
        return;
    }

    /* 普通 HTTP：解析路径并返回静态文件 */
    char method[8] = {0}, path[512] = {0};
    if (sscanf((char *)c->rbuf, "%7s %511s", method, path) == 2) {
        serve_static(srv, c, path);
    } else {
        c->state = CST_CLOSING;
    }
    c->rlen = 0;
}

/* ---------- 处理 WS 入站（ping/close 等控制帧） ---------- */

static void handle_ws_inbound(client_t *c)
{
    for (;;) {
        ws_frame_t fr;
        int rc = ws_decode_frame(c->rbuf, c->rlen, &fr);
        if (rc == ERR_AGAIN)
            break; /* 不完整，等更多数据 */
        if (rc != ERR_OK) {
            c->state = CST_CLOSING;
            break;
        }

        if (fr.opcode == WS_OP_CLOSE) {
            c->state = CST_CLOSING;
        } else if (fr.opcode == WS_OP_PING) {
            uint8_t pong[140];
            int n = ws_encode_frame(fr.payload, fr.payload_len, WS_OP_PONG,
                                    pong, sizeof(pong));
            if (n > 0)
                client_queue(c, pong, (size_t)n);
        }
        /* 其余（如客户端发来的控制消息）M4 再处理 */

        /* 推进缓冲 */
        size_t consumed = fr.frame_len;
        if (consumed >= c->rlen) {
            c->rlen = 0;
        } else {
            memmove(c->rbuf, c->rbuf + consumed, c->rlen - consumed);
            c->rlen -= consumed;
        }
    }
}

/* ---------- 服务创建 ---------- */

int server_create(server_t **out, const server_opts_t *opts)
{
    if (!out || !opts)
        return ERR_INVAL;

    server_t *srv = calloc(1, sizeof(*srv));
    if (!srv)
        return ERR_NOMEM;

    pthread_mutex_init(&srv->lock, NULL);
    snprintf(srv->web_root, sizeof(srv->web_root), "%s",
             opts->web_root ? opts->web_root : "web");

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        log_error("socket() failed: %s", strerror(errno));
        free(srv);
        return ERR_IO;
    }

    int yes = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)opts->port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind() failed on port %d: %s", opts->port, strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return ERR_IO;
    }

    if (listen(srv->listen_fd, 16) < 0) {
        log_error("listen() failed: %s", strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return ERR_IO;
    }

    if (set_nonblocking(srv->listen_fd) != ERR_OK) {
        close(srv->listen_fd);
        free(srv);
        return ERR_IO;
    }

    srv->epoll_fd = epoll_create1(0);
    if (srv->epoll_fd < 0) {
        log_error("epoll_create1() failed: %s", strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return ERR_IO;
    }

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.ptr = NULL;   /* NULL 表示监听套接字 */
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev);

    log_info("server listening on 0.0.0.0:%d, web_root=%s",
             opts->port, srv->web_root);

    *out = srv;
    return ERR_OK;
}

/* 注册新客户端到 epoll 与列表（需持锁修改列表） */
static void add_client(server_t *srv, client_t *c)
{
    pthread_mutex_lock(&srv->lock);
    if (srv->nclients >= MAX_CLIENTS) {
        pthread_mutex_unlock(&srv->lock);
        log_warn("too many clients, rejecting %s", c->ip);
        client_free(c);
        return;
    }
    srv->clients[srv->nclients++] = c;
    pthread_mutex_unlock(&srv->lock);

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.ptr = c;
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, c->fd, &ev);
}

/* 从 epoll 与列表移除并释放客户端（需持锁修改列表） */
static void remove_client(server_t *srv, client_t *c)
{
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);

    pthread_mutex_lock(&srv->lock);
    for (int i = 0; i < srv->nclients; ++i) {
        if (srv->clients[i] == c) {
            srv->clients[i] = srv->clients[--srv->nclients];
            break;
        }
    }
    pthread_mutex_unlock(&srv->lock);

    log_debug("client %s disconnected (fd=%d)", c->ip, c->fd);
    client_free(c);
}

/* 接受所有挂起连接 */
static void accept_clients(server_t *srv)
{
    for (;;) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(srv->listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }
        set_nonblocking(cfd);
        int one = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); /* 低延迟 */

        client_t *c = client_new(cfd, inet_ntoa(caddr.sin_addr));
        if (!c) { close(cfd); continue; }
        add_client(srv, c);
        log_debug("client connected %s (fd=%d)", c->ip, cfd);
    }
}

/* 处理某客户端可读 */
static void on_readable(server_t *srv, client_t *c)
{
    for (;;) {
        if (c->rlen >= sizeof(c->rbuf) - 1) {
            /* 缓冲满还没成帧，异常，关闭 */
            c->state = CST_CLOSING;
            return;
        }
        ssize_t n = recv(c->fd, c->rbuf + c->rlen,
                         sizeof(c->rbuf) - 1 - c->rlen, 0);
        if (n > 0) {
            c->rlen += (size_t)n;
            if (c->state == CST_HTTP)
                handle_http(srv, c);
            else if (c->state == CST_WS)
                handle_ws_inbound(c);
            if (c->state == CST_CLOSING)
                return;
        } else if (n == 0) {
            c->state = CST_CLOSING;  /* 对端关闭 */
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            c->state = CST_CLOSING;
            return;
        }
    }
}

int server_run(server_t *srv, volatile bool *running)
{
    if (!srv || !running)
        return ERR_INVAL;

    struct epoll_event events[MAX_EVENTS];

    while (*running) {
        int nfds = epoll_wait(srv->epoll_fd, events, MAX_EVENTS, 200);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            log_error("epoll_wait failed: %s", strerror(errno));
            return ERR_IO;
        }

        for (int i = 0; i < nfds; ++i) {
            client_t *c = events[i].data.ptr;

            if (c == NULL) {          /* 监听套接字 */
                accept_clients(srv);
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                c->state = CST_CLOSING;
            }
            if (events[i].events & EPOLLIN) {
                on_readable(srv, c);
            }
            if (events[i].events & EPOLLOUT) {
                pthread_mutex_lock(&srv->lock);
                client_flush(c);
                pthread_mutex_unlock(&srv->lock);
            }

            /* 发送缓冲处理 + 关注可写事件 */
            pthread_mutex_lock(&srv->lock);
            int has_pending = (c->slen > c->soff);
            if (has_pending) {
                if (client_flush(c))
                    epoll_mod(srv->epoll_fd, c->fd, EPOLLIN | EPOLLOUT, c);
                else
                    epoll_mod(srv->epoll_fd, c->fd, EPOLLIN, c);
            }
            int closing = (c->state == CST_CLOSING) &&
                          (c->slen <= c->soff); /* 发完才关 */
            pthread_mutex_unlock(&srv->lock);

            if (closing)
                remove_client(srv, c);
        }
    }

    /* 退出时清理所有客户端 */
    pthread_mutex_lock(&srv->lock);
    int n = srv->nclients;
    client_t *snapshot[MAX_CLIENTS];
    memcpy(snapshot, srv->clients, sizeof(client_t *) * (size_t)n);
    srv->nclients = 0;
    pthread_mutex_unlock(&srv->lock);
    for (int i = 0; i < n; ++i) {
        epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, snapshot[i]->fd, NULL);
        client_free(snapshot[i]);
    }

    log_info("server loop exited");
    return ERR_OK;
}

int server_broadcast(server_t *srv, const uint8_t *data, size_t len)
{
    if (!srv || !data || len == 0)
        return 0;

    /* 封装成一个 WebSocket 二进制帧 */
    size_t cap = len + 14;
    uint8_t *frame = malloc(cap);
    if (!frame)
        return 0;
    int flen = ws_encode_frame(data, len, WS_OP_BIN, frame, cap);
    if (flen <= 0) {
        free(frame);
        return 0;
    }

    int sent = 0;
    pthread_mutex_lock(&srv->lock);
    for (int i = 0; i < srv->nclients; ++i) {
        client_t *c = srv->clients[i];
        if (c->state != CST_WS)
            continue;
        if (client_queue(c, frame, (size_t)flen) == ERR_OK) {
            client_flush(c);
            if (c->slen > c->soff)  /* 没发完，关注可写 */
                epoll_mod(srv->epoll_fd, c->fd, EPOLLIN | EPOLLOUT, c);
            sent++;
        } else {
            log_warn("client %s send buffer full, dropping frame", c->ip);
        }
    }
    pthread_mutex_unlock(&srv->lock);

    free(frame);
    return sent;
}

int server_client_count(server_t *srv)
{
    if (!srv) return 0;
    pthread_mutex_lock(&srv->lock);
    int n = 0;
    for (int i = 0; i < srv->nclients; ++i)
        if (srv->clients[i]->state == CST_WS)
            n++;
    pthread_mutex_unlock(&srv->lock);
    return n;
}

void server_destroy(server_t *srv)
{
    if (!srv)
        return;
    if (srv->epoll_fd >= 0)
        close(srv->epoll_fd);
    if (srv->listen_fd >= 0)
        close(srv->listen_fd);
    pthread_mutex_destroy(&srv->lock);
    free(srv);
}
