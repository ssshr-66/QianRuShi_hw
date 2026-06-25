#!/usr/bin/env bash
# run.sh - 启动脚本（交付要求）
#
# 用法:
#   ./scripts/run.sh                  # 默认端口 8080
#   ./scripts/run.sh -p 9000 -v       # 指定端口并开 debug 日志
#   ./scripts/run.sh -s 1280x720      # 缩放到 720p
#
# 所有参数透传给可执行文件（见 desktop_stream --help）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

BIN="build/desktop_stream"

if [[ ! -x "$BIN" ]]; then
    echo "Binary not found: $BIN"
    echo "Build first: ./scripts/build.sh"
    exit 1
fi

# 默认指向源码树里的 web 目录托管静态页面
exec "$BIN" --web-root web "$@"
