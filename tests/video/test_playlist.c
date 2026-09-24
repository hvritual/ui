#define _POSIX_C_SOURCE 200809L
#include "video/standby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static uint64_t clock_ms(void) { struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void nap(void) { struct timespec t={0,2000000};nanosleep(&t,NULL); }
static int install(const char *media,const char *store,const char *key,const char *zip) {
 pid_t p=fork();if(p<0)return 0;
 if(!p){execl(media,media,"video-install",store,key,zip,(char *)NULL);_exit(127);}
 int status;return waitpid(p,&status,0)==p&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int rollback(const char *media,const char *store,const char *key) {
 pid_t p=fork();if(p<0)return 0;
 if(!p){execl(media,media,"video-rollback",store,key,(char *)NULL);_exit(127);}
 int status;return waitpid(p,&status,0)==p&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"PLAYLIST_FAIL line=%d %s error=%s\n",__LINE__,#x,p.error?p.error:"none");failed=1;goto done;}}while(0)
int main(int argc,char **argv) {
 if(argc!=6)return 2;
 int failed=0;StandbyVideo p={0};char key[2048],a[2048],b[2048],once[2048],first[65];
 CHECK(snprintf(key,sizeof(key),"%s/video.public",argv[5])<(int)sizeof(key));
 CHECK(snprintf(a,sizeof(a),"%s/video-a.zip",argv[5])<(int)sizeof(a));
 CHECK(snprintf(b,sizeof(b),"%s/video-b.zip",argv[5])<(int)sizeof(b));
 CHECK(snprintf(once,sizeof(once),"%s/video-once.zip",argv[5])<(int)sizeof(once));
 CHECK(install(argv[4],argv[2],key,!strcmp(argv[1],"mixed")?b:!strcmp(argv[1],"once")?once:a));
 uint64_t base=10000,start=clock_ms();
 CHECK(standby_init(&p,argv[3],base));CHECK(standby_reload(&p,argv[2],base,1)==1);memcpy(first,p.plan.generation,65);
 CHECK(standby_step(&p,base+1500,1)==1);base+=1500;
 if(!strcmp(argv[1],"update")) {
  while(!p.surface.valid&&clock_ms()-start<5000){CHECK(standby_step(&p,base+clock_ms()-start,1)>=0);nap();}
  CHECK(p.surface.valid&&p.starts==1);
  CHECK(install(argv[4],argv[2],key,b));
  CHECK(standby_reload(&p,argv[2],base+clock_ms()-start,1)==0&&!strcmp(first,p.plan.generation));
  CHECK(!standby_input(&p,&(InputFrame){.contact_count=1,.contacts={{0,150,170}}},base+clock_ms()-start));
  CHECK(standby_reload(&p,argv[2],base+clock_ms()-start,1)==0);
  CHECK(!standby_input(&p,&(InputFrame){0},base+clock_ms()-start));
  CHECK(p.mode==VIDEO_BUSINESS);
  CHECK(standby_reload(&p,argv[2],base+clock_ms()-start,0)==0);
  CHECK(standby_reload(&p,argv[2],base+clock_ms()-start,1)==1&&strcmp(first,p.plan.generation));
  CHECK(p.plan.item_count==3&&p.applied==2);
  CHECK(rollback(argv[4],argv[2],key));
  CHECK(standby_reload(&p,argv[2],base+clock_ms()-start,1)==1&&!strcmp(first,p.plan.generation)&&p.applied==3);
  puts("PASS video-live-update-deferred-through-wake-safe-apply-and-rollback");
 } else if(!strcmp(argv[1],"mixed")) {
  unsigned visited=1U<<p.item;
  while(!p.loops&&clock_ms()-start<15000){CHECK(standby_step(&p,base+clock_ms()-start,1)>=0);visited|=1U<<p.item;nap();}
  CHECK(p.loops==1&&p.starts==2&&p.worker.decoded==60&&p.faults==0&&visited==7);
  puts("PASS video-mixed-poster-main-and-baseline-playlist-loops");
 } else if(!strcmp(argv[1],"once")) {
  while(!p.finished&&clock_ms()-start<10000){CHECK(standby_step(&p,base+clock_ms()-start,1)>=0);nap();}
  CHECK(p.finished&&p.surface.valid&&p.starts==1&&p.loops==0&&p.worker.decoded==30);
  uint64_t shown=p.worker.shown,seq=p.surface.sequence;
  CHECK(standby_step(&p,base+clock_ms()-start+10000,1)>=0);
  CHECK(p.finished&&p.starts==1&&p.worker.shown==shown&&p.surface.sequence==seq);
  puts("PASS video-single-play-holds-final-frame-without-respawn");
 } else {failed=1;goto done;}
 done:standby_close(&p);
 if(failed||p.worker.pid)return 1;
 puts("VIDEO_PLAYLIST_OK real_worker=true physical_video_validated=false");return 0;
}
