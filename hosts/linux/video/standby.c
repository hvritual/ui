#define _POSIX_C_SOURCE 200809L
#include "standby.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static uint64_t next_session(StandbyVideo *p) { if(p->session==UINT64_MAX)return 0;return ++p->session; }
static int still(StandbyVideo *p,unsigned index) {
 VideoAsset *a=&p->plan.assets[index];
 if(a->kind!=1||!next_session(p))return 0;
 size_t size=(size_t)a->width*a->height*4,off=0;
 while(off<size){ssize_t n=pread(a->fd,p->worker.payload+off,size-off,(off_t)off);if(n<=0)return 0;off+=(size_t)n;}
 uint8_t h[64]={0};memcpy(h,"PUIVFR1\0",8);video_put32(h+8,1);video_put32(h+12,a->width);video_put32(h+16,a->height);
 video_put32(h+20,a->width*4);video_put32(h+24,(uint32_t)size);video_put32(h+28,1);video_put64(h+32,p->session);video_put64(h+40,1);
 video_surface_reset(&p->surface,p->session);return video_surface_accept(&p->surface,h,p->worker.payload,size);
}
static void fault(StandbyVideo *p,const char *reason) {
 p->error=reason;p->faults++;p->failed_generation=1;video_worker_stop(&p->worker);
 video_surface_reset(&p->surface,next_session(p));p->mode=VIDEO_FALLBACK;
 if(!still(p,p->plan.poster)){p->mode=VIDEO_BUSINESS;p->restore_ui=1;}
 fprintf(stderr,"VIDEO_FALLBACK reason=%s session=%llu\n",reason,(unsigned long long)p->session);
}
static int start_item(StandbyVideo *p,uint64_t now) {
 if(!video_worker_reap(&p->worker))return 0;
 VideoItem *i=&p->plan.items[p->item];VideoAsset *a=&p->plan.assets[i->asset];
 p->origin_ms=now;p->clock_started=0;p->finished=0;
 if(a->kind==1) {
  if(!still(p,i->asset)){fault(p,"still-read");return -1;}
  p->clock_started=1;p->mode=VIDEO_PLAYING;return 1;
 }
 uint64_t id=next_session(p);if(!id){fault(p,"session-overflow");return -1;}
 video_surface_reset(&p->surface,id);
 if(!video_worker_start(&p->worker,p->decoder,a->fd,id,a->width,a->height,a->fps_num,a->fps_den,now)){fault(p,"worker-start");return -1;}
 p->starts++;p->mode=VIDEO_PLAYING;
 fprintf(stderr,"VIDEO_START session=%llu item=%u decode=%ux%u fps=%u/%u\n",(unsigned long long)id,p->item,a->width,a->height,a->fps_num,a->fps_den);
 return 1;
}
int standby_init(StandbyVideo *p,const char *decoder,uint64_t now) {
 if(!p||!decoder)return 0;
 memset(p,0,sizeof(*p));video_plan_init(&p->plan);p->decoder=decoder;p->last_activity_ms=now;
 if(!video_worker_init(&p->worker))return 0;
 if(!video_surface_init(&p->surface)){video_worker_close(&p->worker);return 0;}
 p->initialized=1;return 1;
}
int standby_reload(StandbyVideo *p,const char *store,uint64_t now,int safe) {
 if(!p||!p->initialized||!safe||p->mode==VIDEO_HELD_EXIT)return 0;
 VideoPlan next;video_plan_init(&next);
 int r=video_plan_load(store,p->plan.generation,&next);if(r!=1)return r;
 if(p->mode!=VIDEO_BUSINESS){video_plan_close(&next);return 0;}
 video_plan_close(&p->plan);p->plan=next;p->applied++;p->failed_generation=0;p->error=NULL;p->last_activity_ms=now;
 fprintf(stderr,"VIDEO_PLAN_APPLIED generation=%s\n",p->plan.generation);return 1;
}
void standby_preempt(StandbyVideo *p,uint64_t now,int priority) {
 if(!p||!p->initialized)return;
 p->priority=priority;p->last_activity_ms=now;
 if(p->mode!=VIDEO_BUSINESS) {
  video_worker_stop(&p->worker);video_surface_reset(&p->surface,next_session(p));
  p->mode=VIDEO_HELD_EXIT;p->restore_ui=1;p->paused=0;p->exits++;
 }
}
int standby_input(StandbyVideo *p,const InputFrame *f,uint64_t now) {
 if(!p||!f||!p->initialized)return 0;
 p->last_activity_ms=now;
 if(p->mode==VIDEO_BUSINESS)return 1;
 if(p->mode==VIDEO_HELD_EXIT) {
  if(!f->contact_count&&!f->cancelled_count&&!f->suppressed){p->mode=VIDEO_BUSINESS;p->last_activity_ms=now;}
  return 0;
 }
 if(f->contact_count||f->cancelled_count||f->suppressed){standby_preempt(p,now,p->priority);return 0;}
 return 0;
}
void standby_pause(StandbyVideo *p,uint64_t now,int paused) {
 if(!p||p->mode==VIDEO_BUSINESS||p->mode==VIDEO_HELD_EXIT)return;
 if(paused&&!p->paused){p->paused=1;p->pause_ms=now;}
 if(!paused&&p->paused){if(now>=p->pause_ms)p->origin_ms+=now-p->pause_ms;p->paused=0;p->worker.activity_ms=now;}
}
int standby_step(StandbyVideo *p,uint64_t now,int safe) {
 if(!p||!p->initialized)return -1;
 (void)video_worker_reap(&p->worker);
 if(now<p->last_poll_ms){fault(p,"clock-reversed");return -1;}p->last_poll_ms=now;
 if(p->priority)return 0;
 if(p->mode==VIDEO_BUSINESS) {
  if(!safe||p->failed_generation||!p->plan.item_count||now<p->last_activity_ms||now-p->last_activity_ms<p->plan.idle_ms)return 0;
  p->item=0;p->mode=VIDEO_STARTING;p->origin_ms=now;
 }
 if(p->mode==VIDEO_STARTING) {
  int r=start_item(p,now);
  if(!r&&now>p->origin_ms&&now-p->origin_ms>1000)fault(p,"worker-not-reaped");
  return r;
 }
 if(p->mode!=VIDEO_PLAYING)return 0;
 if(p->paused){p->worker.activity_ms=now;return 0;}
 const VideoItem *i=&p->plan.items[p->item];const VideoAsset *a=&p->plan.assets[i->asset];
 uint64_t elapsed=p->clock_started&&now>=p->origin_ms?(now-p->origin_ms)*1000:0;
 if(a->kind==2) {
  int r=video_worker_poll(&p->worker,&p->surface,now,elapsed,0);
  if(r<0){fault(p,p->worker.error?p->worker.error:"decoder");return -1;}
  if(p->surface.valid&&!p->clock_started){p->origin_ms=now;p->clock_started=1;elapsed=0;}
  if(!p->worker.eof||elapsed<p->worker.last_pts+1000000ULL*a->fps_den/a->fps_num)return r;
 } else if(!p->clock_started||elapsed<(uint64_t)i->hold_ms*1000)return 0;
 if(p->finished)return 0;
 if(p->item+1==p->plan.item_count&&!p->plan.loop){p->finished=1;return 0;}
 video_worker_stop(&p->worker);
 if(++p->item==p->plan.item_count){p->item=0;p->loops++;}
 p->mode=VIDEO_STARTING;p->origin_ms=now;return 0;
}
void standby_close(StandbyVideo *p) {
 if(!p||!p->initialized)return;
 video_worker_close(&p->worker);video_surface_close(&p->surface);video_plan_close(&p->plan);p->initialized=0;
}
