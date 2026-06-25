#!/usr/bin/env bash
# build.sh - 一键编译脚本（交付要求）
#
# 用法:
#   ./scripts/build.sh           # Release 构建
#   ./scripts/build.sh debug     # Debug 构建（带调试符号）
#   ./scripts/build.sh asan      # AddressSanitizer 构建（查内存问题）
set -euo pipefail

# 切到项目根目录（脚本所在目录的上一级）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

MODE="${1:-release}"
BUILD_DIR="build"

case "$MODE" in
    release)
        CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
        ;;
    debug)
        CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug"
        ;;
    asan)
        CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-fsanitize=address,undefined -g"
        BUILD_DIR="build-asan"
        ;;
    *)
        echo "Usage: $0 [release|debug|asan]"
        exit 1
        ;;
esac

echo ">>> Configuring ($MODE) ..."
cmake -B "$BUILD_DIR" $CMAKE_ARGS

echo ">>> Building ..."
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build OK -> $BUILD_DIR/desktop_stream"
echo "Run with: ./scripts/run.sh"
