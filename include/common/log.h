/*
 * log.h - 轻量级线程安全分级日志
 *
 * 用法：
 *   log_set_level(LOG_INFO);
 *   log_info("server listening on port=%d", port);
 *   log_error("recv failed: %s", strerror(errno));
 *
 * 约定（详见 .trellis/spec/backend/logging-guidelines.md）：
 *   - 热路径（每帧）只能用 log_debug，默认不输出
 *   - 一次日志调用 = 一行原子输出，多线程不会交错
 */
#ifndef COMMON_LOG_H
#define COMMON_LOG_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

/* 设置全局日志级别，低于该级别的日志不输出。默认 LOG_INFO */
void log_set_level(log_level_t level);

/* 内部实现，不要直接调用，使用下面的宏 */
void log_write(log_level_t lvl, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define log_debug(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* COMMON_LOG_H */
