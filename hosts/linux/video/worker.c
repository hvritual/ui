#define _GNU_SOURCE
#include "worker.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
static int worker_fail(VideoWorker *w,const char *s) {
 if(!w->error){w->error=s;w->failures++;}
 video_worker_stop(w);return -1;
}
int video_worker_init(VideoWorker *w) {
 if(!w)return 0;
 memset(w,0,sizeof(*w));w->fd=-1;w->payload=malloc(VIDEO_FRAME_BYTES);return w->payload!=NULL;
}
int video_worker_reap(VideoWorker *w) {
 if(!w||w->pid<=0)return 1;
 struct rusage r={0};int status=0;pid_t p=wait4(w->pid,&status,WNOHANG,&r);
 if(p==0)return 0;
 if(p<0&&errno==EINTR)return 0;
 if(p<0&&errno!=ECHILD)return 0;
 if(p>0) {
  w->cpu_us+=(uint64_t)r.ru_utime.tv_sec*1000000+r.ru_utime.tv_usec+(uint64_t)r.ru_stime.tv_sec*1000000+r.ru_stime.tv_usec;
  if(r.ru_maxrss>w->peak_rss_kib)w->peak_rss_kib=r.ru_maxrss;
 }
 w->pid=0;w->reaped++;return 1;
}
void video_worker_stop(VideoWorker *w) {
 if(!w)return;
 if(w->fd>=0){close(w->fd);w->fd=-1;}
 if(w->pid>0)(void)kill(w->pid,SIGKILL);
 w->stopped=1;w->ready=0;(void)video_worker_reap(w);
}
int video_worker_start(VideoWorker *w,const char *exe,int asset_fd,uint64_t session,unsigned width,unsigned height,unsigned fn,unsigned fd,uint64_t now) {
 if(!w||!w->payload||!exe||asset_fd<0||!session||!video_dimensions(width,height)||!fn||!fd||fn>30U*fd)return 0;
 if(!video_worker_reap(w))return 0;
 struct stat st;
 if(lstat(exe,&st)||!S_ISREG(st.st_mode)||(st.st_mode&0022)||(st.st_uid!=0&&st.st_uid!=geteuid()))return 0;
 int pipes[2];if(pipe2(pipes,O_CLOEXEC))return 0;
 char s[32],ww[16],hh[16],fpsn[16],fpsd[16];
 snprintf(s,sizeof(s),"%llu",(unsigned long long)session);snprintf(ww,sizeof(ww),"%u",width);snprintf(hh,sizeof(hh),"%u",height);
 snprintf(fpsn,sizeof(fpsn),"%u",fn);snprintf(fpsd,sizeof(fpsd),"%u",fd);
 int input=fcntl(asset_fd,F_DUPFD_CLOEXEC,10);
 if(input<0){close(pipes[0]);close(pipes[1]);return 0;}
 pid_t parent=getpid(),pid=fork();
 if(pid<0){close(input);close(pipes[0]);close(pipes[1]);return 0;}
 if(pid==0) {
  close(pipes[0]);
  if(dup2(pipes[1],STDOUT_FILENO)<0||dup2(input,3)<0)_exit(125);
  close(input);if(pipes[1]!=STDOUT_FILENO&&pipes[1]!=3)close(pipes[1]);
  struct rlimit mem={96U*1024U*1024U,96U*1024U*1024U},cpu={120,120},core={0,0};
  if(setrlimit(RLIMIT_AS,&mem)||setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_CORE,&core))_exit(125);
  if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))_exit(125);
  execl(exe,exe,"3",s,ww,hh,fpsn,fpsd,(char *)NULL);_exit(127);
 }
 close(input);close(pipes[1]);
 int flags=fcntl(pipes[0],F_GETFL);
 if(flags<0||fcntl(pipes[0],F_SETFL,flags|O_NONBLOCK)<0){close(pipes[0]);w->pid=pid;video_worker_stop(w);return 0;}
 w->pid=pid;w->fd=pipes[0];w->session=session;w->last_seq=w->last_pts=0;
 w->header_used=w->payload_used=w->payload_need=0;w->ready=w->eof=w->stopped=0;w->error=NULL;
 w->activity_ms=w->started_ms=now;w->expected_w=width;w->expected_h=height;return 1;
}
static int header_valid(VideoWorker *w) {
 const uint8_t *h=w->header;
 if(memcmp(h,"PUIVFR1\0",8)||video_u64(h+32)!=w->session)return 0;
 unsigned type=video_u32(h+8),bytes=video_u32(h+24);
 if(type==2){if(bytes||!w->last_seq||video_u64(h+40)!=w->last_seq||video_u64(h+48)!=w->last_pts)return 0;w->eof=1;return 1;}
 if(type!=1||video_u32(h+12)!=w->expected_w||video_u32(h+16)!=w->expected_h||video_u32(h+20)!=w->expected_w*4||
    bytes!=(size_t)w->expected_w*w->expected_h*4||bytes>VIDEO_FRAME_BYTES||video_u32(h+28)!=1||
    video_u64(h+40)!=w->last_seq+1||video_u64(h+40)>VIDEO_FRAME_LIMIT||video_u64(h+48)>VIDEO_DURATION_US||
    (!w->last_seq&&video_u64(h+48)!=0)||(w->last_seq&&video_u64(h+48)<=w->last_pts))return 0;
 w->payload_need=bytes;return 1;
}
int video_worker_poll(VideoWorker *w,VideoSurface *surface,uint64_t now,uint64_t play_us,int paused) {
 if(!w||!surface||w->error)return -1;
 (void)video_worker_reap(w);
 if(paused){w->activity_ms=now;return 0;}
 if(w->stopped||w->eof)return 0;
 size_t budget=2U*1024U*1024U;int changed=0;
 while(budget&&w->fd>=0) {
  if(w->ready) {
   uint64_t pts=video_u64(w->header+48);
   if(pts>play_us)return changed;
   if(pts+250000<play_us&&surface->valid){w->dropped++;}
   else {
    if(!video_surface_accept(surface,w->header,w->payload,w->payload_need))return worker_fail(w,"VIDEO_SURFACE_REJECTED");
    w->shown++;changed=1;
   }
   w->ready=0;w->header_used=w->payload_used=w->payload_need=0;
  }
  uint8_t *p;size_t n;
  if(w->header_used<VIDEO_WIRE_BYTES){p=w->header+w->header_used;n=VIDEO_WIRE_BYTES-w->header_used;}
  else {p=w->payload+w->payload_used;n=w->payload_need-w->payload_used;}
  if(n>budget)n=budget;
  ssize_t count=read(w->fd,p,n);
  if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
  if(count<0&&errno==EINTR)break;
  if(count<=0)return worker_fail(w,count==0?"VIDEO_TRUNCATED_OR_WORKER_EXIT":"VIDEO_PIPE_READ");
  budget-=(size_t)count;w->activity_ms=now;
  if(w->header_used<VIDEO_WIRE_BYTES) {
   w->header_used+=(size_t)count;
   if(w->header_used==VIDEO_WIRE_BYTES) {
    if(!header_valid(w))return worker_fail(w,"VIDEO_HEADER_SESSION_OR_BUDGET");
    if(w->eof){close(w->fd);w->fd=-1;return changed;}
   }
  } else {
   w->payload_used+=(size_t)count;
   if(w->payload_used==w->payload_need) {
    w->ready=1;w->last_seq=video_u64(w->header+40);w->last_pts=video_u64(w->header+48);w->decoded++;
    w->decode_us+=video_u32(w->header+60);
   }
  }
 }
 if(!w->ready&&now>w->activity_ms&&now-w->activity_ms>5000)return worker_fail(w,"VIDEO_DECODE_TIMEOUT");
 return changed;
}
void video_worker_close(VideoWorker *w) {
 if(!w)return;
 video_worker_stop(w);
 for(unsigned i=0;i<50&&w->pid>0;i++){struct timespec t={0,5000000};nanosleep(&t,NULL);(void)video_worker_reap(w);}
 free(w->payload);w->payload=NULL;
}
