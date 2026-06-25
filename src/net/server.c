/*
 * server.c - 最小 HTTP 静态文件服务（M0 可运行版）
 *
 * 用 epoll 监听，接受连接，解析极简 HTTP GET，返回 web_root 下的静态文件。
 * 这是为了让 M0 阶段"浏览器能打开页面"，验证网络链路。
 *
 * M3/M4 待扩展：
 *   - 完整的 epoll 边沿触发 + 非阻塞客户端状态机
 *   - WebSocket 握手(Sec-WebSocket-Accept) + 帧 (un)masking
 *   - 向所有客户端广播编码帧（packet_t），按客户端做背压/丢帧
 */
#include "net/server.h"
#include "common/error.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define MAX_EVENTS 64
#define RECV_BUF   8192

struct server {
    int listen_fd;
    int epoll_fd;
    char web_root[512];
};

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return ERR_IO;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return ERR_IO;
    return ERR_OK;
}

int server_create(server_t **out, const server_opts_t *opts)
{
    if (!out || !opts)
        return ERR_INVAL;

    server_t *srv = calloc(1, sizeof(*srv));
    if (!srv)
        return ERR_NOMEM;

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
    ev.events  = EPOLLIN;
    ev.data.fd = srv->listen_fd;
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev);

    log_info("server listening on 0.0.0.0:%d, web_root=%s",
             opts->port, srv->web_root);

    *out = srv;
    return ERR_OK;
}

/* 极简：把请求路径映射到文件并返回。仅支持 GET。 */
static void handle_client(server_t *srv, int fd)
{
    char buf[RECV_BUF];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(fd);
        return;
    }
    buf[n] = '\0';

    /* 解析 "GET /path HTTP/1.1" */
    char method[8] = {0}, path[512] = {0};
    if (sscanf(buf, "%7s %511s", method, path) != 2) {
        close(fd);
        return;
    }

    /* 根路径映射到 index.html */
    char relpath[512];
    if (strcmp(path, "/") == 0)
        snprintf(relpath, sizeof(relpath), "index.html");
    else
        snprintf(relpath, sizeof(relpath), "%s", path + 1); /* 去掉开头 '/' */

    /* 防目录穿越：拒绝包含 ".." 的路径 */
    if (strstr(relpath, "..")) {
        const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send(fd, resp, strlen(resp), 0);
        close(fd);
        return;
    }

    char fullpath[1100];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", srv->web_root, relpath);

    FILE *fp = fopen(fullpath, "rb");
    if (!fp) {
        const char *resp =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n\r\nNot Found";
        send(fd, resp, strlen(resp), 0);
        close(fd);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* 简单的 content-type 判断 */
    const char *ctype = "application/octet-stream";
    if (strstr(relpath, ".html")) ctype = "text/html; charset=utf-8";
    else if (strstr(relpath, ".js")) ctype = "application/javascript";
    else if (strstr(relpath, ".css")) ctype = "text/css";

    char header[256];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %ld\r\n"
                        "Connection: close\r\n\r\n",
                        ctype, fsize);
    send(fd, header, hlen, 0);

    char filebuf[4096];
    size_t rd;
    while ((rd = fread(filebuf, 1, sizeof(filebuf), fp)) > 0)
        send(fd, filebuf, rd, 0);

    fclose(fp);
    close(fd);
    log_debug("served %s (%ld bytes)", relpath, fsize);
}

int server_run(server_t *srv, volatile bool *running)
{
    if (!srv || !running)
        return ERR_INVAL;

    struct epoll_event events[MAX_EVENTS];

    while (*running) {
        int nfds = epoll_wait(srv->epoll_fd, events, MAX_EVENTS, 500);
        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            log_error("epoll_wait failed: %s", strerror(errno));
            return ERR_IO;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == srv->listen_fd) {
                /* 接受所有挂起的连接 */
                for (;;) {
                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof(caddr);
                    int cfd = accept(srv->listen_fd,
                                     (struct sockaddr *)&caddr, &clen);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        break;
                    }
                    log_debug("client connected fd=%d ip=%s",
                              cfd, inet_ntoa(caddr.sin_addr));
                    /* M0 简化：同步处理后关闭。M3 改为 epoll 管理客户端状态机。 */
                    handle_client(srv, cfd);
                }
            }
        }
    }

    log_info("server loop exited");
    return ERR_OK;
}

void server_destroy(server_t *srv)
{
    if (!srv)
        return;
    if (srv->epoll_fd >= 0)
        close(srv->epoll_fd);
    if (srv->listen_fd >= 0)
        close(srv->listen_fd);
    free(srv);
}
