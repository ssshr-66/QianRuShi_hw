#include "pipeline/types.h"

#include <stdlib.h>

frame_t *frame_alloc(int width, int height, int stride)
{
    if (width <= 0 || height <= 0 || stride <= 0)
        return NULL;

    frame_t *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    f->size = (size_t)stride * (size_t)height;
    f->data = malloc(f->size);
    if (!f->data) {
        free(f);
        return NULL;
    }

    f->width  = width;
    f->height = height;
    f->stride = stride;
    return f;
}

void frame_free(frame_t *f)
{
    if (!f)
        return;
    free(f->data);
    free(f);
}

packet_t *packet_alloc(size_t size)
{
    packet_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;

    if (size > 0) {
        p->data = malloc(size);
        if (!p->data) {
            free(p);
            return NULL;
        }
        p->size = size;
    }
    return p;
}

void packet_free(packet_t *p)
{
    if (!p)
        return;
    free(p->data);
    free(p);
}
