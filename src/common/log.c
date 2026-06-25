#include "common/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/* 当前日志级别，默认 INFO */
static log_level_t g_level = LOG_INFO;

/* 保证一次日志输出为一行，多线程不交错 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(log_level_t lvl)
{
    switch (lvl) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    default:        return "?????";
    }
}

void log_set_level(log_level_t level)
{
    g_level = level;
}

void log_write(log_level_t lvl, const char *file, int line, const char *fmt, ...)
{
    if (lvl < g_level)
        return;

    /* 时间戳 */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    /* 只取文件名，去掉路径前缀 */
    const char *base = file;
    for (const char *p = file; *p; ++p) {
        if (*p == '/')
            base = p + 1;
    }

    pthread_mutex_lock(&g_lock);

    fprintf(stderr, "%s.%03ld %s [%s:%d] [tid=%lu] ",
            timebuf, ts.tv_nsec / 1000000, level_str(lvl), base, line,
            (unsigned long)pthread_self());

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
    fflush(stderr);

    pthread_mutex_unlock(&g_lock);
}
