#include "common/config.h"
#include "common/error.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void config_init_defaults(server_config_t *cfg)
{
    cfg->port     = DEFAULT_PORT;
    cfg->fps      = DEFAULT_FPS;
    cfg->bitrate  = DEFAULT_BITRATE;
    cfg->scale_w  = DEFAULT_SCALE_W;
    cfg->scale_h  = DEFAULT_SCALE_H;
    cfg->verbose  = false;
    cfg->web_root = "web";
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  -p, --port PORT       listen port (default %d)\n", DEFAULT_PORT);
    printf("  -f, --fps FPS         target frame rate (default %d)\n", DEFAULT_FPS);
    printf("  -b, --bitrate BPS     encode bitrate (default %d)\n", DEFAULT_BITRATE);
    printf("  -s, --scale WxH       output scale, e.g. 1280x720 (default: native)\n");
    printf("  -r, --web-root DIR    static web assets dir (default \"web\")\n");
    printf("  -v, --verbose         enable debug logging\n");
    printf("  -h, --help            show this help\n");
}

int config_parse_args(server_config_t *cfg, int argc, char **argv)
{
    static struct option long_opts[] = {
        {"port",     required_argument, 0, 'p'},
        {"fps",      required_argument, 0, 'f'},
        {"bitrate",  required_argument, 0, 'b'},
        {"scale",    required_argument, 0, 's'},
        {"web-root", required_argument, 0, 'r'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:f:b:s:r:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p':
            cfg->port = atoi(optarg);
            break;
        case 'f':
            cfg->fps = atoi(optarg);
            break;
        case 'b':
            cfg->bitrate = atoi(optarg);
            break;
        case 's':
            if (sscanf(optarg, "%dx%d", &cfg->scale_w, &cfg->scale_h) != 2) {
                fprintf(stderr, "invalid scale format: %s (expected WxH)\n", optarg);
                return ERR_INVAL;
            }
            break;
        case 'r':
            cfg->web_root = optarg;
            break;
        case 'v':
            cfg->verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 1; /* 请求帮助，调用方应退出 */
        default:
            print_usage(argv[0]);
            return ERR_INVAL;
        }
    }

    if (cfg->port <= 0 || cfg->port > 65535) {
        fprintf(stderr, "invalid port: %d\n", cfg->port);
        return ERR_INVAL;
    }
    if (cfg->fps <= 0 || cfg->fps > 120) {
        fprintf(stderr, "invalid fps: %d\n", cfg->fps);
        return ERR_INVAL;
    }

    return ERR_OK;
}

void config_print(const server_config_t *cfg)
{
    log_info("config: port=%d fps=%d bitrate=%d scale=%dx%d web_root=%s verbose=%d",
             cfg->port, cfg->fps, cfg->bitrate,
             cfg->scale_w, cfg->scale_h, cfg->web_root, cfg->verbose);
}
