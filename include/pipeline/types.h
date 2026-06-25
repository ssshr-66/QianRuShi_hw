/*
 * types.h - 流水线中传递的数据结构
 *
 * frame_t  : 捕获阶段产出的一帧原始图像（capture -> encode）
 * packet_t : 编码阶段产出的一个压缩包（encode -> network）
 *
 * 所有权约定：谁从队列取出，谁负责释放（用 *_free 释放）。
 */
#ifndef PIPELINE_TYPES_H
#define PIPELINE_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* 原始帧（捕获输出）。具体像素格式在 M1/M2 细化，这里先放通用字段。 */
typedef struct {
    uint8_t *data;      /* 像素数据缓冲区 */
    size_t   size;      /* data 字节数 */
    int      width;     /* 宽 */
    int      height;    /* 高 */
    int      stride;    /* 每行字节数 */
    uint64_t timestamp; /* 捕获时间戳(ms)，用于延迟测算 */
} frame_t;

/* 编码后的压缩包（编码输出）。 */
typedef struct {
    uint8_t *data;       /* 编码字节流 */
    size_t   size;       /* data 字节数 */
    uint64_t timestamp;  /* 对应帧时间戳(ms) */
    int      is_keyframe;/* 是否关键帧(I帧) */
} packet_t;

/* 分配/释放帮助函数（M1/M2 会用到，这里先声明） */
frame_t  *frame_alloc(int width, int height, int stride);
void      frame_free(frame_t *f);

packet_t *packet_alloc(size_t size);
void      packet_free(packet_t *p);

#endif /* PIPELINE_TYPES_H */
