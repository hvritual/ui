#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "input/cli.h"
#include "input/live.h"
#include "display/fbdev.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static uint64_t clock_ns=1000000000ULL;
static unsigned step_no, reconnects, observed_clicks, observed_cancels, observed_pressed;
static int no_input;
void pocket_bench_stage(int stage){(void)stage;}
int __wrap_host_monotonic_ns(uint64_t *out){clock_ns+=100000ULL;*out=clock_ns;return 1;}
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout){(void)fds;(void)n;if(timeout>0)clock_ns+=(uint64_t)timeout*1000000ULL;return 0;}
int __wrap_input_live_discover(InputLive *l,const char *dir,const InputLiveConfig *cfg){
 (void)dir;memset(l,0,sizeof(*l));l->opened=1;l->fd=90;l->config=*cfg;
 strcpy(l->path,"/fixture/event1");strcpy(l->name,"ilitek_ts");
 InputTransform t={.x={0,16384},.y={0,16384},.width=1024,.height=600};
 return input_state_init(&l->state,INPUT_PROTOCOL_MT_B,10,&t);
}
void __wrap_input_live_close(InputLive *l){l->opened=0;l->fd=-1;}
int __wrap_input_live_reconnect(InputLive *l,const char *dir){
 (void)dir;l->opened=1;l->fd=91;l->reconnects++;l->reconnect_attempts++;reconnects++;
 strcpy(l->path,"/fixture/event2");
 InputTransform t={.x={0,16384},.y={0,16384},.width=1024,.height=600};
 return input_state_init(&l->state,INPUT_PROTOCOL_MT_B,10,&t);
}
int __wrap_input_live_wait(InputLive *l,int ms){(void)l;if(ms>0)clock_ns+=(uint64_t)ms*1000000ULL;return !no_input&&step_no<4;}
int __wrap_input_live_drain(InputLive *l,InputFrameSink sink,void *ctx){
 if(step_no==1){input_state_disconnect(&l->state);l->disconnects++;l->error="INPUT_DEVICE_DISCONNECTED";l->system_errno=ENODEV;step_no++;return -1;}
 int result=1;
 if(step_no==0||step_no==2){
  result &= input_state_feed(&l->state,EV_ABS,ABS_MT_TRACKING_ID,(int)step_no+10)!=0;
  result &= input_state_feed(&l->state,EV_ABS,ABS_MT_POSITION_X,step_no==0?1100:14000)!=0;
  result &= input_state_feed(&l->state,EV_ABS,ABS_MT_POSITION_Y,2400)!=0;
 }else result &= input_state_feed(&l->state,EV_ABS,ABS_MT_TRACKING_ID,-1)!=0;
 result &= input_state_feed(&l->state,EV_SYN,SYN_REPORT,0)==2;
 l->events+=4;l->frames++;step_no++;
 return result&&sink(ctx,input_state_frame(&l->state),clock_ns)?1:-1;
}
int __wrap_fbdev_open(FbDevice *d,const char *path,int writable){(void)path;memset(d,0,sizeof(*d));d->opened=1;d->writable=writable;return 1;}
int __wrap_fbdev_close(FbDevice *d){d->opened=0;return 1;}
int __wrap_fbdev_present(void *ctx,const HostFrame *frame){
 FbDevice *d=ctx;if(frame->width!=1024||frame->height!=600||!frame->pixels)return 0;
 d->presents++;
 if(!pocket_runtime_harness_bind("inspect"))return 0;
 int32_t n=0;if(!pocket_runtime_harness_call(1,0,&n))return 0;observed_clicks=(unsigned)n;
 if(!pocket_runtime_harness_call(3,0,&n))return 0;
 observed_cancels=(unsigned)n;
 if(!pocket_runtime_harness_call(4,0,&n))return 0;
 observed_pressed=(unsigned)n;
 return 1;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 char report[1024],trace[1024];snprintf(report,sizeof(report),"%s/loop.json",argv[1]);snprintf(trace,sizeof(trace),"%s/timeline.csv",argv[1]);
 char *args[]={"ui-host","--touch-test","--profile","imx6ul-1024x600","--asset-root",argv[1],"--ticks","60","--output",report,"--trace-output",trace};
 int rc=input_cli(12,args);
 if(rc||reconnects!=1||observed_clicks!=1||observed_cancels!=1||observed_pressed){
  fprintf(stderr,"TOUCH_LOOP_FAILED rc=%d reconnect=%u clicks=%u cancels=%u pressed=%u\n",rc,reconnects,observed_clicks,observed_cancels,observed_pressed);return 1;
 }
 puts("PASS touch-loop-disconnect-cancel-reconnect-real-guest");
 no_input=1;char no_report[1024];snprintf(no_report,sizeof(no_report),"%s/no-input.json",argv[1]);
 char *empty[]={"ui-host","--touch-test","--profile","imx6ul-1024x600","--asset-root",argv[1],"--ticks","2","--output",no_report};
 if(input_cli(10,empty)!=1||host_alloc_stats().live_bytes){fputs("TOUCH_LOOP_FAILED empty-or-leak\n",stderr);return 1;}
 puts("PASS touch-loop-no-events-cannot-pass-acceptance");
 puts("TOUCH_LOOP_OK cases=2 io=fixture real_core=true");return 0;
}
