#define _POSIX_C_SOURCE 200809L
#include "host.h"
#include "input/cli.h"
#include "input/live.h"
#include "display/fbdev.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>

/* Run the actual production media-scene CLI + actual PocketJS with only I/O and
 * time virtualized. No synthetic timing is accepted as performance evidence. */
static uint64_t time_ns=1000000000ULL;
static unsigned presentations;
void pocket_bench_stage(int stage){(void)stage;}
int __wrap_host_monotonic_ns(uint64_t *out){time_ns+=100000ULL;*out=time_ns;return 1;}
int __wrap_poll(struct pollfd *fds,nfds_t n,int ms){(void)fds;(void)n;if(ms>0)time_ns+=(uint64_t)ms*1000000ULL;return 0;}
int __wrap_input_live_discover(InputLive *l,const char *dir,const InputLiveConfig *cfg){
 (void)dir;memset(l,0,sizeof(*l));l->opened=1;l->fd=90;l->config=*cfg;
 strcpy(l->path,"/fixture/event1");strcpy(l->name,"ilitek_ts");
 InputTransform t={.x={0,16384},.y={0,16384},.width=1024,.height=600};
 return input_state_init(&l->state,INPUT_PROTOCOL_MT_B,10,&t);
}
void __wrap_input_live_close(InputLive *l){l->opened=0;l->fd=-1;}
int __wrap_input_live_reconnect(InputLive *l,const char *dir){(void)l;(void)dir;return 0;}
int __wrap_input_live_wait(InputLive *l,int ms){(void)l;if(ms>0)time_ns+=(uint64_t)ms*1000000ULL;return 0;}
int __wrap_input_live_drain(InputLive *l,InputFrameSink f,void *c){(void)l;(void)f;(void)c;return 0;}
int __wrap_fbdev_open(FbDevice *d,const char *path,int writable){(void)path;memset(d,0,sizeof(*d));d->opened=1;d->writable=writable;return 1;}
int __wrap_fbdev_close(FbDevice *d){d->opened=0;return 1;}
int __wrap_fbdev_present(void *context,const HostFrame *f){
 FbDevice *d=context;if(!f||!f->pixels||f->width!=1024||f->height!=600)return 0;
 d->presents++;d->bytes_written+=(uint64_t)f->width*f->height*4;presentations++;return 1;
}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 char *args[]={"ui-host","--touch-test","--profile","imx6ul-1024x600","--app","media-scene",
  "--asset-root",argv[1],"--ticks","60","--output",argv[2],"--trace-output",argv[3]};
 int rc=input_cli(14,args);
 if(rc||presentations!=2||host_alloc_stats().live_bytes){
  fprintf(stderr,"SCENE_IDLE_LOOP_FAILED rc=%d presents=%u leaks=%zu\n",rc,presentations,host_alloc_stats().live_bytes);return 1;
 }
 puts("SCENE_IDLE_LOOP_OK guest_turns=61 presents=2 clean_skips=30 timing=fixture");return 0;
}
