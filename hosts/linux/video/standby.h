#ifndef COFFEE_VIDEO_STANDBY_H
#define COFFEE_VIDEO_STANDBY_H
#include "worker.h"
#include "plan.h"
#include "../input/state.h"
typedef enum { VIDEO_BUSINESS,VIDEO_STARTING,VIDEO_PLAYING,VIDEO_FALLBACK,VIDEO_HELD_EXIT } VideoMode;
typedef struct {
 VideoPlan plan;
 VideoWorker worker;
 VideoSurface surface;
 const char *decoder;
 VideoMode mode;
 uint64_t session,last_activity_ms,origin_ms,pause_ms,last_poll_ms,starts,exits,loops,faults,applied;
 unsigned item;
 int initialized,paused,clock_started,restore_ui,priority,failed_generation,finished;
 const char *error;
} StandbyVideo;
int standby_init(StandbyVideo *p,const char *decoder,uint64_t now_ms);
int standby_reload(StandbyVideo *p,const char *store,uint64_t now_ms,int ui_safe);
/* Returns 1 only when the frame may reach business UI; consumes wake's entire gesture. */
int standby_input(StandbyVideo *p,const InputFrame *frame,uint64_t now_ms);
void standby_preempt(StandbyVideo *p,uint64_t now_ms,int priority);
void standby_pause(StandbyVideo *p,uint64_t now_ms,int paused);
int standby_step(StandbyVideo *p,uint64_t now_ms,int ui_safe);
void standby_close(StandbyVideo *p);
#endif
