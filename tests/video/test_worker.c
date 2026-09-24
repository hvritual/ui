#define _POSIX_C_SOURCE 200809L
#include "video/standby.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void sleep_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};nanosleep(&t,NULL);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"VIDEO_WORKER_FAIL line=%d %s error=%s\n",__LINE__,#x,p.error?p.error:"none");failed=1;goto done;}}while(0)
int main(int argc,char **argv) {
 if(argc!=4)return 2;
 int failed=0;StandbyVideo p={0};uint64_t start=now_ms(),base=10000;uint8_t *canvas=NULL;
 CHECK(standby_init(&p,argv[2],base));CHECK(standby_reload(&p,argv[1],base,1)==1);CHECK(p.plan.asset_count==3);
 CHECK(standby_step(&p,base+1499,1)==0&&p.mode==VIDEO_BUSINESS);
 CHECK(standby_step(&p,base+1500,1)==1);CHECK(p.starts==1&&p.mode==VIDEO_PLAYING);
 base+=1500;int paused=0,first=0;
 while(now_ms()-start<12000&&p.loops<1) {
  uint64_t n=base+now_ms()-start;
  CHECK(standby_step(&p,n,1)>=0);
  if(p.surface.valid&&!first) {
   first=1;canvas=calloc(1,1024*600*4);CHECK(canvas);CHECK(video_surface_blit(&p.surface,canvas,1024*600*4,1024,600,4096,(VideoRect){0,0,1024,600}));
   FILE *f=fopen(argv[3],"wb");CHECK(f);fprintf(f,"P6\n1024 600\n255\n");for(unsigned i=0;i<1024*600*4;i+=4){uint8_t rgb[]={canvas[i+2],canvas[i+1],canvas[i]};if(fwrite(rgb,1,3,f)!=3){fclose(f);failed=1;goto done;}}CHECK(!fclose(f));
  }
  if(p.worker.shown>=3&&!paused) {
   uint64_t old=p.worker.shown;standby_pause(&p,n,1);sleep_ms(200);CHECK(standby_step(&p,base+now_ms()-start,1)>=0&&p.worker.shown==old);
   standby_pause(&p,base+now_ms()-start,0);paused=1;
  }
  sleep_ms(2);
 }
 CHECK(first&&paused&&p.loops==1&&p.worker.decoded==30&&p.worker.shown>3&&p.faults==0);
 puts("PASS video-real-worker-pts-pause-loop-and-bounded-queue");
 InputFrame down={.contact_count=1,.contacts={{0,160,170}}},up={0};
 CHECK(!standby_input(&p,&down,base+now_ms()-start));CHECK(p.mode==VIDEO_HELD_EXIT&&!p.surface.valid);
 CHECK(!standby_input(&p,&up,base+now_ms()-start+1));CHECK(p.mode==VIDEO_BUSINESS);
 for(int i=0;i<100&&!video_worker_reap(&p.worker);i++)sleep_ms(2);
 CHECK(!p.worker.pid);CHECK(p.worker.reaped>=1);
 puts("PASS video-real-worker-touch-exit-invalidates-and-reaps");
 done:free(canvas);standby_close(&p);
 if(failed||p.worker.pid)return 1;
 printf("VIDEO_WORKER_OK reaped=%llu cpu_us=%llu physical_video_validated=false\n",(unsigned long long)p.worker.reaped,(unsigned long long)p.worker.cpu_us);return 0;
}
