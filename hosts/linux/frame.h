#ifndef COFFEE_HOST_FRAME_H
#define COFFEE_HOST_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* Opaque BGRA bytes; borrowed only for the synchronous present call. */
typedef struct {
    const uint8_t *pixels;
    uint32_t width, height, stride;
    size_t length;
} HostFrame;
typedef int (*HostPresenter)(void *context, const HostFrame *frame);
#endif
