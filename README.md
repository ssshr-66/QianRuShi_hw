# 局域网桌面流媒体（Linux C 服务端 + 浏览器 Web 端）

实时捕获 Linux 桌面，H.264 编码，通过自定义 WebSocket 帧协议低延迟推送到同一局域网的浏览器观看，无需安装任何客户端插件。

> 训练项目。技术栈：X11/XShm 捕获 + FFmpeg(libavcodec) H.264 编码 + epoll 并发 +
> 自定义二进制帧协议(WebSocket) + 浏览器 Canvas/WebCodecs 解码渲染。

---

## 环境要求

**必须在带 X11 桌面的 Linux 上运行**（捕获 API 是 Linux 专属）。开发机若为 macOS，请使用 Linux 虚拟机（见 `.trellis/tasks/.../M0-setup-guide.md`）。

安装依赖（Ubuntu 22.04/24.04）：

```bash
sudo apt update && sudo apt install -y \
  build-essential cmake pkg-config git gdb valgrind \
  libx11-dev libxext-dev libxfixes-dev libxrandr-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev ffmpeg
```

确认处于 X11 会话：`echo $XDG_SESSION_TYPE` 应输出 `x11`。

---

## 编译与运行（一键）

```bash
# 一键编译
./scripts/build.sh           # Release
./scripts/build.sh debug     # Debug
./scripts/build.sh asan      # AddressSanitizer

# 启动（默认端口 8080）
./scripts/run.sh
./scripts/run.sh -p 9000 -v          # 指定端口 + debug 日志
./scripts/run.sh -s 1280x720         # 缩放到 720p

# 也可用 Makefile
make && make run
```

启动后，在同一局域网的浏览器打开 `http://<服务端IP>:<端口>/` 即可。
查看服务端 IP：`hostname -I` 或 `ip addr`。

查看所有参数：`./build/desktop_stream --help`

### M1 截屏自测

验证桌面捕获是否工作（抓一帧存成图片）：

```bash
./build/desktop_stream --grab-test
# 成功后会生成 /tmp/grab_test.ppm，用图片查看器打开应能看到当前桌面
xdg-open /tmp/grab_test.ppm     # 或用 GIMP/eog 打开
```

### M2 编码自测

验证 H.264 编码是否工作（抓取+编码约 60 帧存成裸流）：

```bash
./build/desktop_stream --encode-test
# 成功后生成 /tmp/encode_test.h264，用 ffplay 播放应能看到桌面录像
ffplay /tmp/encode_test.h264
```

---

## 当前进度

本仓库目前是 **M0（骨架）** 阶段：
- 完整目录结构、构建系统（CMake + Makefile + 脚本）就绪
- 各模块接口定义齐全
- 服务端可编译运行：探测 X11/FFmpeg 依赖、启动 HTTP 静态服务（浏览器可打开页面）
- 实时捕获/编码/推流（M1~M5）为后续里程碑，代码中以 `TODO(Mx)` 标注

里程碑规划见 `.trellis/tasks/06-24-01-desktop-streaming/prd.md`。

---

## 目录结构

```
include/        # 公共头文件（按模块划分）
  capture/      # 桌面捕获接口
  encode/       # 视频编码接口
  net/          # 网络服务接口
  protocol/     # 帧协议
  pipeline/     # 帧/包结构 + 有界队列
  common/       # 日志/错误/配置
src/            # 实现，与 include 对应
  main.c        # 入口：解析参数 -> 初始化 -> 事件循环
web/            # 浏览器客户端（静态资源，无构建步骤）
scripts/        # build.sh / run.sh 一键脚本
docs/           # 架构图、时序图、性能数据
tests/          # 单元/冒烟测试
```

开发规范见 `.trellis/spec/`（backend = C 服务端，frontend = Web 客户端）。

---

## 注意

- 在 macOS 上用编辑器打开 C 源码会报大量"头文件找不到"，这是因为 Mac 的 clangd
  用的是 macOS SDK，没有 Linux/X11/FFmpeg 头文件。**这些是编辑器误报**，真正编译在
  Linux 虚拟机里用 CMake 进行，不受影响。
