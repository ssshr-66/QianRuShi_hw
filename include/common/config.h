/*
 * config.h - 服务端运行配置
 *
 * 来自命令行参数，描述捕获/编码/网络的关键参数。
 * 后续里程碑会逐步用到这些字段。
 */
#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <stdbool.h>

/* 默认值 */
#define DEFAULT_PORT        8080
#define DEFAULT_FPS         30
#define DEFAULT_BITRATE     4000000   /* 4 Mbps */
#define DEFAULT_SCALE_W     0         /* 0 表示不缩放，用桌面原始宽度 */
#define DEFAULT_SCALE_H     0         /* 0 表示不缩放，用桌面原始高度 */

typedef struct {
    int  port;          /* 监听端口 */
    int  fps;           /* 目标帧率 */
    int  bitrate;       /* 编码码率(bps) */
    int  scale_w;       /* 输出宽度，0=原始 */
    int  scale_h;       /* 输出高度，0=原始 */
    bool verbose;       /* 是否开启 DEBUG 日志 */
    const char *web_root; /* 静态网页根目录，默认 "web" */
    bool grab_test;     /* M1 自测：抓一帧存成图片后退出 */
} server_config_t;

/* 用默认值初始化配置 */
void config_init_defaults(server_config_t *cfg);

/*
 * 解析命令行参数到 cfg。
 * 返回 ERR_OK；遇到 -h/--help 返回 1（调用方据此退出）；出错返回负数。
 */
int config_parse_args(server_config_t *cfg, int argc, char **argv);

/* 打印当前生效的配置（INFO 级别） */
void config_print(const server_config_t *cfg);

#endif /* COMMON_CONFIG_H */
