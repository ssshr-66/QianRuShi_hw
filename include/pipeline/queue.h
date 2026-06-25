/*
 * queue.h - 有界线程安全队列（流水线各阶段之间的核心原语）
 *
 * 设计要点（详见 .trellis/spec/backend/database-guidelines.md）：
 *   - 有界容量，满时按"丢最旧"策略丢弃，保证实时性优先于完整性
 *   - 锁只用于保护队列状态，绝不在持锁时做 I/O / 编码 / 网络
 *   - 用条件变量唤醒消费者，不忙等
 *
 * 队列存储 void* 元素（通常是 frame_t* 或 packet_t*）。
 */
#ifndef PIPELINE_QUEUE_H
#define PIPELINE_QUEUE_H

#include <stddef.h>

typedef struct queue queue_t;

/* 元素释放回调：当"丢最旧"丢弃元素或销毁队列时调用，用于释放元素内存 */
typedef void (*queue_free_fn)(void *item);

/* 创建容量为 capacity 的有界队列。free_fn 可为 NULL（不自动释放）。 */
queue_t *queue_create(size_t capacity, queue_free_fn free_fn);

/* 销毁队列，对残留元素调用 free_fn。 */
void queue_destroy(queue_t *q);

/*
 * 非阻塞入队。队列满时：丢弃最旧元素（对其调用 free_fn），再放入新元素。
 * 返回 0 成功；返回正数表示发生了丢弃（丢弃数量）。
 */
int queue_push_drop_oldest(queue_t *q, void *item);

/*
 * 阻塞出队，最多等待 timeout_ms 毫秒。
 *   - 取到元素：*out 设为元素，返回 0
 *   - 超时：返回 ERR_AGAIN(-7)
 *   - 队列已关闭且为空：返回 ERR_CLOSED(-6)
 * timeout_ms < 0 表示一直等待。
 */
int queue_pop(queue_t *q, void **out, int timeout_ms);

/* 关闭队列：唤醒所有等待者，使 pop 在队空时返回 ERR_CLOSED。 */
void queue_close(queue_t *q);

/* 当前元素个数（仅供监控/日志，非强一致）。 */
size_t queue_size(queue_t *q);

#endif /* PIPELINE_QUEUE_H */
