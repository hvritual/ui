#define _POSIX_C_SOURCE 200809L
#include "framework.h"
#include "display/fbdev.h"
#include "input/live.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef POCKET_BUILD_COMMIT
#define POCKET_BUILD_COMMIT "uncommitted"
#endif
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static PocketEngineStatus present(void *p,const PocketEngineFrame *f){
    HostFrame frame={.pixels=f->pixels,.width=f->width,.height=f->height,.stride=f->stride,.length=f->length};
    return fbdev_present(p,&frame)?POCKET_ENGINE_OK:POCKET_ENGINE_BACKEND_FAILED;
}
static int number(const char *s,int min,int max,int *out){
    if(!s||!*s)return 0;
    char *end;errno=0;long n=strtol(s,&end,10);
    if(errno||*end||n<min||n>max)return 0;
    *out=(int)n;return 1;
}
static FILE *file_at(const char *dir,const char *name){
    char path[4096];if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path))return NULL;
    return fopen(path,"wx");
}
static double cpu_seconds(const struct rusage *r){
    return r->ru_utime.tv_sec+r->ru_stime.tv_sec+(r->ru_utime.tv_usec+r->ru_stime.tv_usec)/1000000.0;
}
int main(int argc,char **argv){
    const char *profile=NULL,*assets=NULL,*output=NULL,*fbpath="/dev/fb0",*inputdir="/dev/input",*media=NULL,*token=NULL,*touch_name=NULL;
    int headless=0,seconds=60,items=8,rawmin=0,rawmax=16384,slots=10,swap=0,ix=0,iy=0;
    unsigned touch_fields=0;
    for(int i=1;i<argc;i++){
        const char *key=argv[i];if(!strcmp(key,"--headless")){headless=1;continue;}
        if(i+1>=argc){fprintf(stderr,"missing value: %s\n",key);return 2;}const char *v=argv[++i];
        if(!strcmp(key,"--profile"))profile=v;else if(!strcmp(key,"--asset-root"))assets=v;
        else if(!strcmp(key,"--output"))output=v;else if(!strcmp(key,"--fbdev"))fbpath=v;
        else if(!strcmp(key,"--input-dir"))inputdir=v;else if(!strcmp(key,"--media-store"))media=v;
        else if(!strcmp(key,"--allow-write"))token=v;
        else if(!strcmp(key,"--touch-name")){touch_name=v;touch_fields|=1;}
        else if(!strcmp(key,"--seconds")){if(!number(v,1,86400,&seconds))return 2;}
        else if(!strcmp(key,"--items")){if(!number(v,8,100,&items)||(items!=8&&items!=100))return 2;}
        else if(!strcmp(key,"--raw-min")){if(!number(v,-65536,65536,&rawmin))return 2;touch_fields|=2;}
        else if(!strcmp(key,"--raw-max")){if(!number(v,-65536,65536,&rawmax))return 2;touch_fields|=4;}
        else if(!strcmp(key,"--slots")){if(!number(v,1,32,&slots))return 2;touch_fields|=8;}
        else if(!strcmp(key,"--swap-xy")){if(!number(v,0,1,&swap))return 2;touch_fields|=16;}
        else if(!strcmp(key,"--invert-x")){if(!number(v,0,1,&ix))return 2;touch_fields|=32;}
        else if(!strcmp(key,"--invert-y")){if(!number(v,0,1,&iy))return 2;touch_fields|=64;}
        else{fprintf(stderr,"unknown option: %s\n",key);return 2;}
    }
    if(!profile||!assets||!output||strlen(output)>3800||
       (strcmp(profile,"imx6ul-1024x600")&&strcmp(profile,"imx6ul-1024x800"))){
        fprintf(stderr,"usage: ui-framework --profile imx6ul-1024x600|imx6ul-1024x800 --asset-root DIR --output NEWDIR [--headless | --allow-write I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER]\n");return 2;
    }
    unsigned h=!strcmp(profile,"imx6ul-1024x600")?600U:800U;
    if(!headless&&(!token||strcmp(token,"I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER")))return 2;
    if(!headless&&h==800&&touch_fields!=127){fprintf(stderr,"800 target requires its own explicit verified touch configuration\n");return 2;}
    if(rawmin>=rawmax)return 2;
    if(!touch_name)touch_name="ilitek_ts";
    if(mkdir(output,0700)){perror("new output directory");return 2;}
    FILE *trace=file_at(output,"timeline.csv");if(!trace)return 2;
    fprintf(trace,"sample_ns,input_event_ns,guest_turns,scene_uploads,presents,page,modal,progress,nodes,pool,first,selected,locale,theme\n");
    PocketFramework *r=calloc(1,sizeof(*r));if(!r){fclose(trace);return 1;}
    FbDevice fb={0};InputLive input={0};int ok=0,unblank_errno=0;const char *failure="INITIALIZATION";
    uint64_t start=0,now=0,next_reconnect=0;struct rusage before={0},after={0};CoffeeAppStats stats={0};
    if(getrusage(RUSAGE_SELF,&before)){failure="USAGE_START";goto done;}
    if(!host_monotonic_ns(&start))goto done;
    now=start;
    if(!headless){
        if(!fbdev_open(&fb,fbpath,1)){failure=fb.error?fb.error:"DISPLAY_OPEN";goto done;}
        if(fb.layout.width!=1024||fb.layout.height!=h){failure="DISPLAY_PROFILE_MISMATCH";goto done;}
        /* Explicit write admission already obtained. No mode set, PAN or VSync. */
        if(ioctl(fb.fd,FBIOBLANK,FB_BLANK_UNBLANK)<0){unblank_errno=errno;failure="DISPLAY_UNBLANK";goto done;}
        FILE *probe=file_at(output,"display.json");if(!probe){failure="PROBE_FILE";goto done;}
        int wrote=fbdev_report(probe,fbpath,&fb);if(fclose(probe)||!wrote){failure="PROBE_WRITE";goto done;}
    }
    PocketDisplayBackend backend={1,sizeof(PocketDisplayBackend),&fb,present};
    if(!pocket_framework_open(r,h,items,assets,media,headless?NULL:&backend)){failure=r->error;goto done;}
    InputLiveConfig cfg={touch_name,1024,h,swap,ix,iy,rawmin,rawmax,(unsigned)slots};
    if(!headless&&!input_live_discover(&input,inputdir,&cfg)){failure=input.error?input.error:"INPUT_DISCOVERY";goto done;}
    signal(SIGINT,stop);signal(SIGTERM,stop);
    HostClock clock;host_clock_start(&clock,start);
    if(!pocket_framework_tick(r,start,1)){failure=r->error;goto done;}
    char snapshot[4096];snprintf(snapshot,sizeof(snapshot),"%s/first.ppm",output);
    if(!pocket_framework_snapshot(r,snapshot)){failure="FIRST_SNAPSHOT";goto done;}
#ifdef POCKET_TEST_SYNTHETIC_IO
    const char *io_mode="synthetic-io";
#else
    const char *io_mode=headless?"headless":"physical-fbdev";
#endif
    fprintf(stderr,"FRAMEWORK_FIRST_PRESENT profile=%s mode=%s commit=%s\n",profile,io_mode,POCKET_BUILD_COMMIT);
    while(!stopping){
        if(!host_monotonic_ns(&now)){failure="CLOCK";goto done;}
        if(now-start>=(uint64_t)seconds*1000000000ULL)break;
        if(!headless){
            if(input.opened){
                int wait=input_live_wait(&input,0);
                if(wait<0|| (wait>0&&input_live_drain(&input,pocket_framework_input,r)<0)){
                    /* poll HUP has no read-side disconnect callback. Count that loss once. */
                    if(!input.state.disconnected){input_state_disconnect(&input.state);input.disconnects++;}
                    pocket_framework_disconnect(r,now);input_live_close(&input);next_reconnect=now+500000000ULL;
                }
            }else if(now>=next_reconnect){(void)input_live_reconnect(&input,inputdir);next_reconnect=now+500000000ULL;}
        }
        if(!host_monotonic_ns(&now)){failure="CLOCK";goto done;}
        int due=host_clock_due(&clock,now);if(due<0){failure="CLOCK_REVERSED";goto done;}
        for(int j=0;j<due;j++)if(!pocket_framework_tick(r,now,0)){failure=r->error;goto done;}
        if(due){
            if(!coffee_app_stats(&r->app,&stats)){failure="STATS";goto done;}
            if(fprintf(trace,"%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",(unsigned long long)now,(unsigned long long)r->event_ns,
                (unsigned long long)r->ticks,(unsigned long long)r->engine.scene_uploads,(unsigned long long)r->presents,
                stats.page,stats.modal,stats.progress,stats.nodes,stats.pool,stats.first,stats.selected,stats.locale,stats.theme)<0){failure="TRACE_WRITE";goto done;}
        }
        if(r->error){failure=r->error;goto done;}
        /* Preserve 60 Hz guest clock; input wakes the bounded wait sooner. */
        uint64_t wait_ns=clock.next_ns>now?clock.next_ns-now:0;int ms=(int)(wait_ns/1000000ULL);if(ms>17)ms=17;
        if(ms<1)ms=1;
        if(!headless&&input.opened)(void)input_live_wait(&input,ms);
        else (void)host_sleep_until(now+(uint64_t)ms*1000000ULL);
    }
    if(!pocket_framework_tick(r,now,1)){failure=r->error;goto done;}
    snprintf(snapshot,sizeof(snapshot),"%s/last.ppm",output);
    if(!pocket_framework_snapshot(r,snapshot)){failure="LAST_SNAPSHOT";goto done;}
    (void)coffee_app_stats(&r->app,&stats);ok=1;failure=NULL;
done:
    if(r->opened){(void)coffee_app_stats(&r->app,&stats);if(!pocket_framework_close(r)){ok=0;failure="ENGINE_CLOSE";}}
    input_live_close(&input);if(!fbdev_close(&fb)){ok=0;failure="DISPLAY_CLOSE";}
    if(input.cleanup_errno){ok=0;failure="INPUT_CLOSE";}
    if(fclose(trace)){ok=0;failure="TRACE_CLOSE";}
    if(getrusage(RUSAGE_SELF,&after)||!host_monotonic_ns(&now)){ok=0;failure="USAGE_END";}
    double wall=now>=start?(now-start)/1000000000.0:0;
    FILE *report=file_at(output,"report.json");
    if(report){
#ifdef POCKET_TEST_SYNTHETIC_IO
        const int physical_io=0;
        fputs("{\"synthetic\":true,",report);
#else
        const int physical_io=!headless&&fb.presents;
        fputc('{',report);
#endif
        fprintf(report,"\"schema\":1,\"commit\":\"%s\",\"profile\":\"%s\",\"ok\":%s,\"physical_io\":%s,\"visual_validated\":false,\"business_commands\":false,\"error\":",
          POCKET_BUILD_COMMIT,profile,ok?"true":"false",physical_io?"true":"false");
        if(failure)fprintf(report,"\"%s\"",failure);else fputs("null",report);
        fprintf(report,",\"wall_seconds\":%.6f,\"cpu_percent_one_core\":%.6f,\"peak_rss_kib\":%ld,\"ticks\":%llu,\"presents\":%llu,\"clean_skips\":%llu,\"bytes_written\":%llu,\"page_mask\":%u,\"modal_seen\":%u,\"completed\":%u,\"pool\":%u,\"nodes\":%u,\"item_count\":%u,\"peak_pool\":%u,\"recycled\":%llu,\"input_frames\":%llu,\"syn_dropped\":%llu,\"disconnects\":%llu,\"reconnects\":%llu,\"timestamp_clamps\":%llu,\"media_applied\":%lu,\"media_rejected\":%lu,\"media_deferred\":%lu,\"unblank_errno\":%d,\"display_cleanup_errno\":%d,\"input_cleanup_errno\":%d,\"core_live_bytes_after_close\":%zu}\n",
         wall,wall>0?100*(cpu_seconds(&after)-cpu_seconds(&before))/wall:0,after.ru_maxrss,
         (unsigned long long)r->ticks,(unsigned long long)r->presents,(unsigned long long)r->clean_skips,
         (unsigned long long)(headless?r->bytes_written:fb.bytes_written),r->page_mask,r->modal_seen,stats.completed,stats.pool,stats.nodes,stats.item_count,stats.peak_pool,(unsigned long long)stats.recycled,
         (unsigned long long)r->input.frames,(unsigned long long)input.syn_dropped,(unsigned long long)input.disconnects,
         (unsigned long long)input.reconnects,(unsigned long long)r->timestamp_clamps,r->media.applied_count,r->media.rejected_count,r->media.deferred_count,
         unblank_errno,fb.cleanup_errno,input.cleanup_errno,host_alloc_stats().live_bytes);
        if(fclose(report))ok=0;
    }else ok=0;
    fprintf(stderr,"FRAMEWORK_EXIT ok=%d error=%s output=%s\n",ok,failure?failure:"none",output);
    free(r);return ok?0:1;
}
