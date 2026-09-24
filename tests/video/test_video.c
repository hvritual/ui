#define _POSIX_C_SOURCE 200809L
#include "video/standby.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"VIDEO_TEST_FAIL line=%d expression=%s\n",__LINE__,#x);failures++;goto done;} }while(0)
static void header(uint8_t *h,uint64_t session,uint64_t seq,uint64_t pts,unsigned w,unsigned ht) {
 memset(h,0,64);memcpy(h,"PUIVFR1\0",8);video_put32(h+8,1);video_put32(h+12,w);video_put32(h+16,ht);
 video_put32(h+20,w*4);video_put32(h+24,w*ht*4);video_put32(h+28,1);video_put64(h+32,session);video_put64(h+40,seq);video_put64(h+48,pts);
}
static void surface_case(void) {
 VideoSurface s={0};uint8_t h[64],pixels[16*16*4],out[40*40*4];
 CHECK(video_surface_init(&s));video_surface_reset(&s,7);
 for(unsigned i=0;i<sizeof(pixels);i+=4){pixels[i]=10;pixels[i+1]=20;pixels[i+2]=240;pixels[i+3]=255;}
 header(h,7,1,0,16,16);CHECK(video_surface_accept(&s,h,pixels,sizeof(pixels)));
 CHECK(!video_surface_accept(&s,h,pixels,sizeof(pixels)));
 video_put64(h+32,6);video_put64(h+40,2);CHECK(!video_surface_accept(&s,h,pixels,sizeof(pixels)));
 memset(out,99,sizeof(out));CHECK(video_surface_blit(&s,out,sizeof(out),40,40,160,(VideoRect){4,8,32,16}));
 CHECK(out[(8*40+12)*4]==10&&out[(8*40+12)*4+2]==240);
 CHECK(out[(7*40+12)*4]==99&&out[(8*40+11)*4]==99&&out[(24*40+12)*4]==99);
 CHECK(!video_surface_blit(&s,out,sizeof(out)-1,40,40,160,(VideoRect){0,0,40,40}));
 CHECK(!video_surface_blit(&s,out,sizeof(out),40,40,160,(VideoRect){39,0,2,2}));
 video_surface_reset(&s,8);CHECK(!s.valid&&!s.dirty);
 puts("PASS video-surface-bounds-session-and-contain");
 done:video_surface_close(&s);
}
static void gesture_case(void) {
 StandbyVideo p={0};InputFrame f={0};CHECK(standby_init(&p,"/no/decoder",100));
 p.mode=VIDEO_PLAYING;p.session=4;
 f.contact_count=1;f.contacts[0]=(InputContact){0,100,100};
 CHECK(!standby_input(&p,&f,200));CHECK(p.mode==VIDEO_HELD_EXIT&&p.restore_ui&&p.session==5);
 f.contacts[0].x=500;CHECK(!standby_input(&p,&f,220));
 f.contact_count=0;CHECK(!standby_input(&p,&f,250));CHECK(p.mode==VIDEO_BUSINESS);
 f.contact_count=1;CHECK(standby_input(&p,&f,300));
 p.mode=VIDEO_PLAYING;f.contact_count=0;f.suppressed=1;
 CHECK(!standby_input(&p,&f,350));CHECK(!standby_input(&p,&f,360));
 f.suppressed=0;f.cancelled_count=1;f.cancelled[0]=0;CHECK(!standby_input(&p,&f,380));
 f.cancelled_count=0;CHECK(!standby_input(&p,&f,400));CHECK(p.mode==VIDEO_BUSINESS);
 p.mode=VIDEO_PLAYING;p.origin_ms=500;p.clock_started=1;
 standby_pause(&p,800,1);CHECK(p.paused);standby_pause(&p,1800,0);CHECK(!p.paused&&p.origin_ms==1500);
 standby_preempt(&p,1900,1);CHECK(p.priority&&p.mode==VIDEO_HELD_EXIT&&p.restore_ui);
 puts("PASS video-wake-consumes-whole-gesture-pause-and-priority");
 done:standby_close(&p);standby_close(&p);
}
static int send_bytes(int fd,const void *p,size_t size) {
 const uint8_t *b=p;while(size){ssize_t n=write(fd,b,size);if(n<0&&errno==EINTR)continue;if(n<=0)return 0;b+=n;size-=(size_t)n;}return 1;
}
static void pipe_case(void) {
 VideoWorker w={.fd=-1};VideoSurface s={0};int fds[2]={-1,-1};uint8_t h[64],pixels[1024]={0};
 CHECK(video_worker_init(&w)&&video_surface_init(&s)&&!pipe(fds));
 int flags=fcntl(fds[0],F_GETFL);CHECK(flags>=0&&!fcntl(fds[0],F_SETFL,flags|O_NONBLOCK));
 w.fd=fds[0];fds[0]=-1;w.session=5;w.expected_w=w.expected_h=16;w.activity_ms=100;video_surface_reset(&s,5);
 header(h,5,1,0,16,16);CHECK(send_bytes(fds[1],h,13));CHECK(video_worker_poll(&w,&s,101,0,0)==0);
 CHECK(send_bytes(fds[1],h+13,51)&&send_bytes(fds[1],pixels,1024));CHECK(video_worker_poll(&w,&s,102,0,0)==1&&w.shown==1);
 header(h,5,2,100000,16,16);CHECK(send_bytes(fds[1],h,64)&&send_bytes(fds[1],pixels,1024));
 CHECK(video_worker_poll(&w,&s,120,10000,0)==0&&w.ready);
 CHECK(video_worker_poll(&w,&s,6000,10000,1)==0&&w.shown==1);
 CHECK(video_worker_poll(&w,&s,6001,100000,0)==1&&w.shown==2);
 header(h,5,3,200000,16,16);CHECK(send_bytes(fds[1],h,64)&&send_bytes(fds[1],pixels,1024));
 CHECK(video_worker_poll(&w,&s,6002,600000,0)==0&&w.dropped==1);
 header(h,4,4,700000,16,16);CHECK(send_bytes(fds[1],h,64));
 CHECK(video_worker_poll(&w,&s,6003,700000,0)==-1&&w.failures==1&&w.stopped);
 puts("PASS video-wire-partial-pts-backpressure-late-drop-stale-session");
 done:if(fds[0]>=0)close(fds[0]);if(fds[1]>=0)close(fds[1]);video_worker_close(&w);video_surface_close(&s);
}
static void timeout_case(void) {
 VideoWorker w={.fd=-1};VideoSurface s={0};int fd[2]={-1,-1};
 CHECK(video_worker_init(&w)&&video_surface_init(&s)&&!pipe(fd));
 CHECK(!fcntl(fd[0],F_SETFL,O_NONBLOCK));w.fd=fd[0];fd[0]=-1;w.activity_ms=10;
 CHECK(video_worker_poll(&w,&s,5011,0,0)==-1&&w.error&&!strcmp(w.error,"VIDEO_DECODE_TIMEOUT"));
 CHECK(w.pid==0&&w.fd==-1);
 puts("PASS video-stalled-decoder-bounded-timeout");
 done:if(fd[0]>=0)close(fd[0]);if(fd[1]>=0)close(fd[1]);video_worker_close(&w);video_surface_close(&s);
}
int main(int argc,char **argv) {
 surface_case();gesture_case();pipe_case();timeout_case();
 if(argc>1&&!strcmp(argv[1],"--intentional-failure"))failures++;
 if(failures)return 1;
 puts("VIDEO_UNIT_OK cases=4 physical_video_validated=false");return 0;
}
