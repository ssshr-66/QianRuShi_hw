#include "pipeline/queue.h"
#include "common/error.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

struct queue {
    void          **items;     /* 环形缓冲区 */
    size_t          capacity;  /* 容量 */
    size_t          count;     /* 当前元素数 */
    size_t          head;      /* 队首索引（出队处） */
    size_t          tail;      /* 队尾索引（入队处） */
    queue_free_fn   free_fn;   /* 元素释放回调 */
    int             closed;    /* 是否已关闭 */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty; /* 有元素可取 */
};

queue_t *queue_create(size_t capacity, queue_free_fn free_fn)
{
    if (capacity == 0)
        return NULL;

    queue_t *q = calloc(1, sizeof(*q));
    if (!q)
        return NULL;

    q->items = calloc(capacity, sizeof(void *));
    if (!q->items) {
        free(q);
        return NULL;
    }

    q->capacity = capacity;
    q->free_fn  = free_fn;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return q;
}

void queue_destroy(queue_t *q)
{
    if (!q)
        return;

    /* 释放残留元素 */
    if (q->free_fn) {
        while (q->count > 0) {
            void *item = q->items[q->head];
            q->head = (q->head + 1) % q->capacity;
            q->count--;
            q->free_fn(item);
        }
    }

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    free(q->items);
    free(q);
}

int queue_push_drop_oldest(queue_t *q, void *item)
{
    if (!q || !item)
        return ERR_INVAL;

    int dropped = 0;

    pthread_mutex_lock(&q->lock);

    if (q->closed) {
        pthread_mutex_unlock(&q->lock);
        return ERR_CLOSED;
    }

    /* 队满：丢弃最旧的一个 */
    if (q->count == q->capacity) {
        void *old = q->items[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        if (q->free_fn)
            q->free_fn(old);
        dropped = 1;
    }

    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);

    return dropped;
}

int queue_pop(queue_t *q, void **out, int timeout_ms)
{
    if (!q || !out)
        return ERR_INVAL;

    pthread_mutex_lock(&q->lock);

    while (q->count == 0 && !q->closed) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&q->not_empty, &q->lock);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec  += 1;
                ts.tv_nsec -= 1000000000L;
            }
            int rc = pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->lock);
                return ERR_AGAIN;
            }
        }
    }

    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->lock);
        return ERR_CLOSED;
    }

    *out = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return ERR_OK;
}

void queue_close(queue_t *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->lock);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

size_t queue_size(queue_t *q)
{
    if (!q)
        return 0;
    pthread_mutex_lock(&q->lock);
    size_t n = q->count;
    pthread_mutex_unlock(&q->lock);
    return n;
}
