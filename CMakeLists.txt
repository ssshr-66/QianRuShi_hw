# ============================================================================
# LAN Desktop Streaming Server - 顶层 CMake 构建脚本
#
# 依赖（Ubuntu）:
#   sudo apt install -y build-essential cmake pkg-config \
#        libx11-dev libxext-dev \
#        libavcodec-dev libavutil-dev libswscale-dev
#
# 构建:
#   cmake -B build && cmake --build build
#   或直接运行 ./scripts/build.sh
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(desktop_stream C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 默认 Release，可用 -DCMAKE_BUILD_TYPE=Debug 切换
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 警告全开，本项目要求零警告
add_compile_options(-Wall -Wextra)

# 导出 compile_commands.json，便于编辑器/clangd 索引
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------------
# 查找依赖库（通过 pkg-config）
# ----------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(X11      REQUIRED x11 xext)
pkg_check_modules(FFMPEG   REQUIRED libavcodec libavutil libswscale)

# ----------------------------------------------------------------------------
# 源文件
# ----------------------------------------------------------------------------
set(SOURCES
    src/main.c
    src/common/error.c
    src/common/log.c
    src/common/config.c
    src/pipeline/types.c
    src/pipeline/queue.c
    src/capture/capture_x11.c
    src/encode/encoder_ffmpeg.c
    src/net/server.c
    src/protocol/frame_proto.c
)

add_executable(desktop_stream ${SOURCES})

target_include_directories(desktop_stream PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(desktop_stream PRIVATE
    Threads::Threads
    ${X11_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)

target_compile_options(desktop_stream PRIVATE
    ${X11_CFLAGS_OTHER}
    ${FFMPEG_CFLAGS_OTHER}
)

# ----------------------------------------------------------------------------
# 把可执行文件输出到 build/ 根，web/ 资源就在源码树，运行时用 --web-root 指向
# ----------------------------------------------------------------------------
set_target_properties(desktop_stream PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "X11    : ${X11_LIBRARIES}")
message(STATUS "FFMPEG : ${FFMPEG_LIBRARIES}")
