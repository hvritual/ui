#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "input/live.h"
#include "display/fbdev.h"
#include "video/cli.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
static unsigned tick,video_frames,actions,seen_after_wake,restored,mode;
static uint64_t due;
static const uint8_t *core_pixels;
static int current;
void pocket_bench_stage(int n){(void)n;}
static uint64_t millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int __real_poll(struct pollfd *,nfds_t,int);
int __wrap_poll(struct pollfd *fds,nfds_t n,int wait) {
 if(n!=2)return __real_poll(fds,n,wait);
 int saved=fds[0].fd;fds[0].fd=-1;int r=__real_poll(fds,n,wait);fds[0].fd=saved;
 if(mode==2&&video_frames>=3&&!tick){tick=1;raise(SIGUSR1);}
 if(mode==0&&video_frames>=3&&tick<4&&millis()>=due){fds[0].revents=POLLIN;return r<0?1:r+1;}
 return r;
}
int __wrap_input_live_discover(InputLive *l,const char *dir,const InputLiveConfig *c) {
 (void)dir;memset(l,0,sizeof(*l));l->fd=90;l->opened=1;l->config=*c;strcpy(l->name,"ilitek_ts");strcpy(l->path,"/fixture/event1");
 InputTransform t={.x={0,16384},.y={0,16384},.width=1024,.height=600};return input_state_init(&l->state,INPUT_PROTOCOL_MT_B,10,&t);
}
void __wrap_input_live_close(InputLive *l){l->opened=0;l->fd=-1;}
int __wrap_input_live_reconnect(InputLive *l,const char *d){(void)l;(void)d;return 0;}
int __wrap_input_live_drain(InputLive *l,InputFrameSink sink,void *ctx) {
 InputFrame f={0};f.sequence=tick+1;f.contact_count=tick%2==0;f.contacts[0]=(InputContact){0,150,170};
 if(tick<2&&current!=0){fprintf(stderr,"VIDEO_LOOP_WAKE_CLICKED\n");return -1;}
 int r=sink(ctx,&f,millis()*1000000);tick++;due=millis()+80;l->events++;l->frames++;return r?1:-1;
}
int __wrap_fbdev_open(FbDevice *d,const char *p,int writable){(void)p;memset(d,0,sizeof(*d));d->opened=1;d->writable=writable;return 1;}
int __wrap_fbdev_close(FbDevice *d){d->opened=0;return 1;}
int __wrap_fbdev_present(void *ctx,const HostFrame *f) {
 FbDevice *d=ctx;if(!f->pixels||f->width!=1024||f->height!=600)return 0;
 if(!core_pixels)core_pixels=f->pixels;
 if(f->pixels!=core_pixels)video_frames++;else if(video_frames)restored++;
 d->presents++;
 return 1;
}
int __real_host_turn_contacts(LinuxHost *,const PocketRuntimeContactsInput *);
int __wrap_host_turn_contacts(LinuxHost *h,const PocketRuntimeContactsInput *c) {
 int r=__real_host_turn_contacts(h,c);int32_t value=-1;
 if(!r||!pocket_runtime_harness_bind("inspect")||!pocket_runtime_harness_call(1,0,&value))return 0;
 current=value;actions=(unsigned)pocket_runtime_action_sequence();
 if(tick==2&&current==0)seen_after_wake++;
 return 1;
}
int main(int argc,char **argv) {
 if(argc!=7)return 2;
 mode=(unsigned)atoi(argv[6]);
 char *args[]={"video-host","--assets",argv[1],"--store",argv[2],"--decoder",argv[3],"--output",argv[4],"--trace",argv[5],"--seconds","5"};
 int r=video_cli(13,args);
 if(mode==1){if(r!=1){fputs("VIDEO_LOOP_FAULT_DID_NOT_FAIL\n",stderr);return 1;}puts("PASS video-production-fault-fallback-no-respawn");return 0;}
 if(r||!video_frames||!restored||host_alloc_stats().live_bytes){fprintf(stderr,"VIDEO_LOOP_FAIL rc=%d frames=%u restored=%u\n",r,video_frames,restored);return 1;}
 if(mode==0&&(tick!=4||!seen_after_wake||actions!=1||current!=1)){fprintf(stderr,"VIDEO_LOOP_FAIL clicks=%u tick=%u wake=%u page=%d\n",actions,tick,seen_after_wake,current);return 1;}
 if(mode==2&&(actions||current)){fputs("VIDEO_PRIORITY_CLICKED\n",stderr);return 1;}
 printf("VIDEO_LOOP_OK io=fixture real_core=true decoder=real video_frames=%u wake_release_home=%u actions=%u physical_video_validated=false\n",video_frames,seen_after_wake,actions);return 0;
}
