#define _POSIX_C_SOURCE 200809L
#include "cli.h"
#include "standby.h"
#include "../host.h"
#include "../input/live.h"
#include "../input/bridge.h"
#include "../display/fbdev.h"
#include "../media/store.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static volatile sig_atomic_t stopped,pause_request,priority_request;
static void signal_request(int sig) {
 if(sig==SIGUSR1)priority_request=1;
 else if(sig==SIGUSR2)pause_request=1;
 else stopped=sig;
}
typedef struct { StandbyVideo *player;InputBridge *bridge;uint64_t now_ms,consumed,wake_gestures; } InputSink;
static int hit(void *unused,float x,float y) { (void)unused;return pocket_runtime_hit_test_bounds(x,y); }
static int ingest(void *context,const InputFrame *f,uint64_t event_ns) {
 InputSink *s=context;VideoMode before=s->player->mode;
 if(!standby_input(s->player,f,s->now_ms)) {
  s->consumed++;
  if(before!=VIDEO_HELD_EXIT&&s->player->mode==VIDEO_HELD_EXIT) {
   s->wake_gestures++;return input_bridge_cancel_all(s->bridge,event_ns);
  }
  return 1;
 }
 return input_bridge_ingest(s->bridge,f,event_ns);
}
static uint64_t cpu_us(long *rss) {
 struct rusage r;if(getrusage(RUSAGE_SELF,&r))return 0;
 *rss=r.ru_maxrss;return (uint64_t)r.ru_utime.tv_sec*1000000+r.ru_utime.tv_usec+(uint64_t)r.ru_stime.tv_sec*1000000+r.ru_stime.tv_usec;
}
static int number(const char *s,unsigned *out) {
 if(!s||!*s)return 0;
 for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return 0;
 errno=0;char *end;unsigned long n=strtoul(s,&end,10);if(errno||*end||n<5||n>300)return 0;*out=(unsigned)n;return 1;
}
static FILE *new_report(const char *path) {
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return NULL;
 FILE *f=fdopen(fd,"w");if(!f)close(fd);return f;
}
int video_cli(int argc,char **argv) {
 const char *root=NULL,*store=NULL,*decoder=NULL,*json_path=NULL,*csv_path=NULL,*fb="/dev/fb0",*input_dir="/dev/input",*error=NULL;
 unsigned seconds=60,seen=0;int ok=0,signals=0;uint64_t now=0,begin=0,deadline=0,retry=0,poll_at=0,start_cpu=0,compose_us=0,present_us=0,video_presents=0;
 LinuxHost host={0};FbDevice display={0};InputLive input={0};InputBridge bridge={0};StandbyVideo player={0};InputSink sink={&player,&bridge,0,0,0};
 uint8_t *canvas=NULL;FILE *report=NULL,*trace=NULL;long peak=0;char generation[65]={0};
 struct sigaction sa={0},previous[4];const int sigs[4]={SIGINT,SIGTERM,SIGUSR1,SIGUSR2};
 for(int i=1;i<argc;i+=2) {
  unsigned bit=0;if(i+1>=argc)goto args;
  if(!strcmp(argv[i],"--assets")){root=argv[i+1];bit=1;}
  else if(!strcmp(argv[i],"--store")){store=argv[i+1];bit=2;}
  else if(!strcmp(argv[i],"--decoder")){decoder=argv[i+1];bit=4;}
  else if(!strcmp(argv[i],"--output")){json_path=argv[i+1];bit=8;}
  else if(!strcmp(argv[i],"--trace")){csv_path=argv[i+1];bit=16;}
  else if(!strcmp(argv[i],"--seconds")){if(!number(argv[i+1],&seconds))goto args;bit=32;}
  else if(!strcmp(argv[i],"--fbdev")){fb=argv[i+1];bit=64;}
  else if(!strcmp(argv[i],"--input-dir")){input_dir=argv[i+1];bit=128;}
  else goto args;
  if((seen&bit)||!argv[i+1][0])goto args;
  seen|=bit;
 }
 if(!root||!store||!decoder||!json_path||!csv_path)goto args;
 report=new_report(json_path);trace=new_report(csv_path);
 if(!report||!trace){error="VIDEO_REPORT_OPEN";goto cleanup;}
 fputs("wall_ms,mode,session,item,decoded,shown,dropped,pts_us,compose_us,present_us,input_events,wake_gestures,ui_cpu_us,decoder_cpu_us\n",trace);
 if(!host_monotonic_ns(&now)){error="VIDEO_CLOCK";goto cleanup;}
 begin=now;deadline=begin+(uint64_t)seconds*1000000000;
 if(!standby_init(&player,decoder,now/1000000)){error="VIDEO_INIT";goto cleanup;}
 if(standby_reload(&player,store,now/1000000,1)!=1){error="VIDEO_STORE_ADMISSION";goto cleanup;}
 InputLiveConfig config={.expected_name="ilitek_ts",.width=1024,.height=600,.expected_raw_min=0,.expected_raw_max=16384,.expected_slots=10};
 if(!input_live_discover(&input,input_dir,&config)){error=input.error;goto cleanup;}
 if(!fbdev_open(&display,fb,1)){error=display.error;goto cleanup;}
 if(!host_open(&host,"imx6ul-1024x600",root,"coffee.js","labels.atlas",now)||!media_builtin(root)||!input_bridge_init(&bridge,hit,NULL)){error="VIDEO_HOME_BOOT";goto cleanup;}
 canvas=malloc((size_t)host.width*host.height*4);if(!canvas){error="VIDEO_CANVAS_MEMORY";goto cleanup;}
 HostFrame home;
 if(!host_render(&host,&home)||!host_present_latest(&host,&home,1,fbdev_present,&display)){error="VIDEO_FIRST_PRESENT";goto cleanup;}
 sa.sa_handler=signal_request;sigemptyset(&sa.sa_mask);stopped=pause_request=priority_request=0;
 for(int i=0;i<4;i++){if(sigaction(sigs[i],&sa,&previous[i])){error="VIDEO_SIGNALS";goto cleanup;}signals++;}
 if(!host_monotonic_ns(&now)){error="VIDEO_CLOCK";goto cleanup;}
 host_clock_start(&host.clock,now);begin=now;deadline=begin+(uint64_t)seconds*1000000000;
 start_cpu=cpu_us(&peak);
 fprintf(stderr,"VIDEO_HOST_READY pid=%ld single_writer=true standby_after_ms=%u\n",(long)getpid(),player.plan.idle_ms);
 while(!stopped) {
  if(!host_monotonic_ns(&now)){error="VIDEO_CLOCK";break;}
  if(now>=deadline)break;
  uint64_t ms=now/1000000;sink.now_ms=ms;
  if(pause_request){pause_request=0;standby_pause(&player,ms,!player.paused);fprintf(stderr,"VIDEO_PAUSE state=%d\n",player.paused);}
  if(priority_request){priority_request=0;standby_preempt(&player,ms,1);if(!input_bridge_cancel_all(&bridge,now)){error="VIDEO_PRIORITY_CANCEL";break;}fprintf(stderr,"VIDEO_PRIORITY_PREEMPT\n");}
  int wait=now>=host.clock.next_ns?0:(int)((host.clock.next_ns-now+999999)/1000000);if(wait>17)wait=17;
  struct pollfd fds[2]={{input.opened?input.fd:-1,POLLIN,0},{(!player.paused&&!player.worker.ready)?player.worker.fd:-1,POLLIN,0}};
  int ready=poll(fds,2,wait);if(ready<0&&errno!=EINTR){error="VIDEO_POLL";break;}
  if(input.opened&&(fds[0].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL))) {
   if((fds[0].revents&(POLLHUP|POLLERR|POLLNVAL))||input_live_drain(&input,ingest,&sink)<0) {
    standby_preempt(&player,ms,1);
    if(!input_bridge_cancel_all(&bridge,now)){error="VIDEO_INPUT_CANCEL";break;}
    if(!input.state.disconnected){input_state_disconnect(&input.state);input.disconnects++;}
    input_live_close(&input);retry=ms+500;fprintf(stderr,"VIDEO_INPUT_LOST playback_preempted=true\n");
   }
  } else if(!input.opened&&ms>=retry) {
   if(input_live_reconnect(&input,input_dir)){player.priority=0;player.mode=VIDEO_BUSINESS;player.last_activity_ms=ms;fprintf(stderr,"VIDEO_INPUT_RECONNECTED\n");}
   retry=ms+500;
  }
  if(!host_monotonic_ns(&now)){error="VIDEO_CLOCK";break;}ms=now/1000000;
  int due=host_clock_due(&host.clock,now);if(due<0){error="VIDEO_CLOCK_REVERSED";break;}
  for(int i=0;i<due;i++) {
   PocketRuntimeContactsInput guest={0};uint64_t event;
   if(!input_bridge_next(&bridge,&guest,&event)||!host_turn_contacts(&host,&guest)){error="VIDEO_GUEST";break;}
  }
  if(error)break;
  int safe=input.opened&&!input.state.suppress_until_all_up&&!bridge.queue_count&&
           pocket_runtime_resource_pack(NULL,0)==1;
  if(ms>=poll_at){int r=standby_reload(&player,store,ms,safe);if(r<0)fprintf(stderr,"VIDEO_PLAN_REJECTED old_retained=true\n");poll_at=ms+1000;}
  (void)standby_step(&player,ms,safe);
  compose_us=present_us=0;int presented=0;
  if(player.surface.valid&&(player.mode==VIDEO_PLAYING||player.mode==VIDEO_FALLBACK||player.mode==VIDEO_STARTING)) {
   if(player.surface.dirty) {
    uint64_t a,b,c;
    if(!host_monotonic_ns(&a)){error="VIDEO_CLOCK";break;}
    memset(canvas,0,(size_t)host.width*host.height*4);
    if(!video_surface_blit(&player.surface,canvas,(size_t)host.width*host.height*4,host.width,host.height,host.width*4,(VideoRect){0,0,host.width,host.height})){error="VIDEO_COMPOSITE";break;}
    if(!host_monotonic_ns(&b)){error="VIDEO_CLOCK";break;}
    HostFrame frame={canvas,host.width,host.height,host.width*4,(size_t)host.width*host.height*4};
    if(!fbdev_present(&display,&frame)||!host_monotonic_ns(&c)){error="VIDEO_PRESENT";break;}
    compose_us=(b-a)/1000;present_us=(c-b)/1000;player.surface.dirty=0;host.presentation_valid=0;video_presents++;presented=1;
   }
  } else if(player.restore_ui||player.mode==VIDEO_BUSINESS||player.mode==VIDEO_HELD_EXIT) {
   if(player.restore_ui||(due>0&&host.turns%2==0)) {
    if(!host_render(&host,&home)||!host_present_latest(&host,&home,player.restore_ui,fbdev_present,&display)){error="VIDEO_HOME_RESTORE";break;}
    if(player.restore_ui){fprintf(stderr,"VIDEO_WAKE_UI_RESTORED consumed_until_all_up=true\n");player.restore_ui=0;}
   }
  }
  if(presented||(due>0&&host.turns%60==0)) {
   uint64_t used=cpu_us(&peak);
   fprintf(trace,"%llu,%u,%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
    (unsigned long long)((now-begin)/1000000),(unsigned)player.mode,(unsigned long long)player.session,player.item,
    (unsigned long long)player.worker.decoded,(unsigned long long)player.worker.shown,(unsigned long long)player.worker.dropped,
    (unsigned long long)player.surface.pts_us,(unsigned long long)compose_us,(unsigned long long)present_us,
    (unsigned long long)input.events,(unsigned long long)sink.wake_gestures,(unsigned long long)(used>=start_cpu?used-start_cpu:0),(unsigned long long)player.worker.cpu_us);
   if(ferror(trace)){error="VIDEO_TRACE_WRITE";break;}
  }
 }
 ok=!error&&!stopped&&player.worker.shown>0&&player.faults==0;
cleanup:
 for(int i=0;i<signals;i++)sigaction(sigs[i],&previous[i],NULL);
 if(host.state==HOST_RUNNING) {
  PocketRuntimeContactsInput terminal={0};uint64_t stamp=0;
  if(bridge.hit_test&&input_bridge_cancel_all(&bridge,now)&&input_bridge_next(&bridge,&terminal,&stamp))
   (void)host_turn_contacts(&host,&terminal);
  HostFrame f;
  if(!host_render(&host,&f)||!host_present_latest(&host,&f,1,fbdev_present,&display))ok=0;
 }
 memcpy(generation,player.plan.generation,sizeof(generation));
 input_live_close(&input);standby_close(&player);host_close(&host);if(!fbdev_close(&display))ok=0;free(canvas);
 uint64_t end=begin;(void)host_monotonic_ns(&end);uint64_t used=cpu_us(&peak);
 if(report) {
  fprintf(report,"{\"schema_version\":1,\"operation\":\"standby-video\",\"ok\":%s,\"error\":",ok?"true":"false");
  if(error)fprintf(report,"\"%s\"",error);else if(player.error)fprintf(report,"\"%s\"",player.error);else fputs("null",report);
  fprintf(report,",\"generation\":\"%s\",\"starts\":%llu,\"decoded\":%llu,\"shown\":%llu,\"dropped\":%llu,\"video_presents\":%llu,\"wake_gestures\":%llu,\"consumed_frames\":%llu,\"loops\":%llu,\"faults\":%llu,\"guest_turns\":%llu,\"ui_cpu_us\":%llu,\"ui_peak_rss_kib\":%ld,\"decoder_cpu_us\":%llu,\"decoder_peak_rss_kib\":%ld,\"wall_ns\":%llu,\"decoder_reaped\":%llu,\"decoder_unreaped\":%s,\"single_framebuffer_writer\":true,\"vsync_enabled\":false,\"pan_enabled\":false,\"physical_video_validated\":false}\n",
   generation,(unsigned long long)player.starts,(unsigned long long)player.worker.decoded,(unsigned long long)player.worker.shown,
   (unsigned long long)player.worker.dropped,(unsigned long long)video_presents,(unsigned long long)sink.wake_gestures,(unsigned long long)sink.consumed,
   (unsigned long long)player.loops,(unsigned long long)player.faults,(unsigned long long)host.turns,(unsigned long long)(used>=start_cpu?used-start_cpu:0),peak,
   (unsigned long long)player.worker.cpu_us,player.worker.peak_rss_kib,(unsigned long long)(end>=begin?end-begin:0),(unsigned long long)player.worker.reaped,player.worker.pid>0?"true":"false");
  int bad=ferror(report);if(fclose(report))bad=1;if(bad)ok=0;
 }
 if(trace&&fclose(trace))ok=0;
 fprintf(stderr,"VIDEO_HOST_%s shown=%llu wake_gestures=%llu faults=%llu physical_video_validated=false\n",ok?"OK":"FAILED",(unsigned long long)player.worker.shown,(unsigned long long)sink.wake_gestures,(unsigned long long)player.faults);
 return ok?0:1;
args:
 fprintf(stderr,"USAGE: video-host --assets DIR --store DIR --decoder EXECUTABLE --output NEW.json --trace NEW.csv [--seconds 5..300] [--fbdev PATH] [--input-dir DIR]\n");return 2;
}
