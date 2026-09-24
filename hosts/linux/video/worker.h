#ifndef COFFEE_VIDEO_WORKER_H
#define COFFEE_VIDEO_WORKER_H
#include "surface.h"
#include <sys/types.h>
#include <sys/resource.h>
typedef struct {
 pid_t pid;
 int fd,eof,ready,stopped;
 uint8_t header[VIDEO_WIRE_BYTES],*payload;
 size_t header_used,payload_used,payload_need;
 uint64_t session,last_seq,last_pts,activity_ms,started_ms,decode_us;
 unsigned expected_w,expected_h;
 uint64_t decoded,shown,dropped,failures,reaped,cpu_us;
 long peak_rss_kib;
 const char *error;
} VideoWorker;
int video_worker_init(VideoWorker *worker);
int video_worker_start(VideoWorker *worker,const char *executable,int asset_fd,uint64_t session,unsigned width,unsigned height,unsigned fps_num,unsigned fps_den,uint64_t now_ms);
/* Nonblocking, bounded 2MiB/iteration; future PTS backpressures the decoder pipe. */
int video_worker_poll(VideoWorker *worker,VideoSurface *surface,uint64_t now_ms,uint64_t playback_us,int paused);
void video_worker_stop(VideoWorker *worker);
int video_worker_reap(VideoWorker *worker);
void video_worker_close(VideoWorker *worker);
#endif
