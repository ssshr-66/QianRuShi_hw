# Makefile - 对 CMake 的便捷包装（满足"一键编译"交付要求）
#
# 用法:
#   make          # 配置并编译（Release）
#   make debug    # Debug 构建（带调试符号）
#   make run      # 编译并运行
#   make clean    # 清理构建产物

BUILD_DIR := build
BIN := $(BUILD_DIR)/desktop_stream

.PHONY: all debug run clean

all:
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD_DIR) -j

debug:
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR) -j

run: all
	@$(BIN) --web-root web

clean:
	@rm -rf $(BUILD_DIR)
	@echo "cleaned $(BUILD_DIR)/"
