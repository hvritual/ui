#ifndef COFFEE_VIDEO_SURFACE_H
#define COFFEE_VIDEO_SURFACE_H
#include "wire.h"
typedef struct { unsigned x,y,width,height; } VideoRect;
typedef struct {
 uint8_t *pixels;
 unsigned width,height;
 uint64_t session,sequence,pts_us;
 int valid,dirty;
} VideoSurface;
int video_surface_init(VideoSurface *surface);
void video_surface_reset(VideoSurface *surface,uint64_t session);
int video_surface_accept(VideoSurface *surface,const uint8_t header[VIDEO_WIRE_BYTES],const uint8_t *pixels,size_t length);
/* Bounded native pixel surface. Aspect-fit into a validated rectangle; no JS copy.
 * Caller owns background/overlays and is the only framebuffer presenter. */
int video_surface_blit(const VideoSurface *surface,uint8_t *dst,size_t length,unsigned width,unsigned height,unsigned stride,VideoRect rectangle);
void video_surface_close(VideoSurface *surface);
#endif
