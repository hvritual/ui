#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/framework.h"
#include "tests/framework/frame_oracle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"APPLICATION_FAIL line=%d %s\n",__LINE__,#x);return 0;}}while(0)
typedef struct {PocketFramework *runtime;FrameOracle oracle;uint64_t now,sequence;} Driver;
static int step(Driver *d,unsigned delta){d->now+=delta;CHECK(pocket_framework_tick(d->runtime,d->now*1000000ULL,1));return 1;}
static int contact(Driver *d,int down,int x,int y){
    InputFrame frame={.sequence=++d->sequence};if(down){frame.contact_count=1;frame.contacts[0]=(InputContact){0,x,y};}
    d->now+=20;CHECK(pocket_framework_input(d->runtime,&frame,d->now*1000000ULL));CHECK(step(d,1));return 1;
}
static int tap(Driver *d,int x,int y){CHECK(contact(d,1,x,y));CHECK(contact(d,0,x,y));return 1;}
static int stats(Driver *d,unsigned page,unsigned selected,unsigned modal,unsigned completed){
    PocketApplicationStats s;CHECK(pocket_application_stats(&d->runtime->app,&s));
    CHECK(s.page==page&&s.selected==selected&&s.modal==modal&&s.completed==completed);return 1;
}
static int open_driver(Driver *d,const char *assets,unsigned height){
    memset(d,0,sizeof(*d));d->now=1000;d->runtime=calloc(1,sizeof(*d->runtime));CHECK(d->runtime);
    CHECK(oracle_open(&d->oracle,1024,height));
    PocketDisplayBackend backend={1,sizeof(backend),&d->oracle,oracle_present};
    CHECK(pocket_framework_open(d->runtime,height,8,assets,NULL,&backend));CHECK(step(d,0));return 1;
}
static int close_driver(Driver *d){
    CHECK(pocket_framework_close(d->runtime));CHECK(!host_alloc_stats().live_bytes);
    oracle_close(&d->oracle);free(d->runtime);return 1;
}
static int capture(Driver *d,const char *output,const char *name,unsigned height){
    char path[4096];CHECK(snprintf(path,sizeof(path),"%s/%s-%u.ppm",output,name,height)<(int)sizeof(path));
    CHECK(pocket_framework_snapshot(d->runtime,path));return 1;
}
static int panel(const char *assets,const char *output,unsigned height){
    Driver d;CHECK(open_driver(&d,assets,height));CHECK(stats(&d,1,0,0,0));
    CHECK(capture(&d,output,"panel-home",height));
    CHECK(tap(&d,180,(int)height-124));CHECK(tap(&d,180,(int)height-124));CHECK(stats(&d,1,2,0,0));
    CHECK(tap(&d,500,(int)height-124));CHECK(stats(&d,1,1,0,0));
    CHECK(capture(&d,output,"panel-one",height));
    CHECK(tap(&d,820,(int)height-124));CHECK(stats(&d,2,1,0,0));
    CHECK(capture(&d,output,"panel-settings",height));
    CHECK(tap(&d,780,(int)height-124));CHECK(stats(&d,2,1,1,0));
    CHECK(capture(&d,output,"panel-modal",height));
    CHECK(tap(&d,230,(int)height-124));CHECK(stats(&d,2,1,1,0)); /* Back cannot penetrate the modal. */
    CHECK(tap(&d,340,348));CHECK(stats(&d,2,1,0,0));
    CHECK(tap(&d,780,(int)height-124));CHECK(tap(&d,650,348));CHECK(stats(&d,1,0,0,1));
    CHECK(capture(&d,output,"panel-reset",height));
    CHECK(tap(&d,180,(int)height-124));CHECK(stats(&d,1,1,0,1));
    CHECK(contact(&d,1,180,(int)height-124));
    d.now+=20;pocket_framework_disconnect(d.runtime,d.now*1000000ULL);
    CHECK(contact(&d,0,180,(int)height-124));CHECK(stats(&d,1,1,0,1));
    CHECK(close_driver(&d));return 1;
}
static int coffee(const char *assets,const char *output,unsigned height){
    Driver d;CHECK(open_driver(&d,assets,height));CHECK(stats(&d,1,0,0,0));
    CHECK(capture(&d,output,"coffee-home",height));
    CHECK(tap(&d,100,200));CHECK(stats(&d,2,0,0,0));
    CHECK(tap(&d,640,(int)height-112));CHECK(stats(&d,2,0,1,0));
    CHECK(tap(&d,640,364));CHECK(stats(&d,3,0,0,0));
    CHECK(step(&d,5001));CHECK(stats(&d,4,0,0,1));
    CHECK(capture(&d,output,"coffee-success",height));
    CHECK(tap(&d,500,(int)height-91));CHECK(stats(&d,1,0,0,1));
    CHECK(tap(&d,675,(int)height-36));CHECK(stats(&d,1,0,1,1));
    char path[4096];CHECK(snprintf(path,sizeof(path),"%s/private-%u.ppm",output,height)<(int)sizeof(path));
    CHECK(!pocket_framework_snapshot(d.runtime,path)&&access(path,F_OK)!=0);
    CHECK(tap(&d,900,40));CHECK(stats(&d,1,0,0,1));
    CHECK(close_driver(&d));return 1;
}
static int source_cases(const char *assets){
    HostAsset source={0};CHECK(host_asset_read(assets,"application.js",POCKET_PROGRAM_SOURCE_LIMIT,&source));
    const char *invalid[]={
      "\nPocketApplication.start=function(){__pocketCall('native.shell',1);return true;};",
      "\nconst initial=PocketApplication.start;PocketApplication.start=function(c){initial(c);const x=__pocketCall('component.create',1,0,0,0,1024,c.height,1,0,0);__pocketCall('navigation.push',2,x);__pocketCall('navigation.pop');__pocketCall('component.text',x,5001);return true;};",
      "\nconst initial=PocketApplication.start;PocketApplication.start=function(c){initial(c);__pocketCall('frame.state',0,0,0,99,0,0,0,0,0,0,0,0,1);return true;};"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        size_t length=source.length+strlen(invalid[i]);char *script=malloc(length+1);CHECK(script);
        memcpy(script,source.data,source.length);memcpy(script+source.length,invalid[i],strlen(invalid[i])+1);
        PocketApplication app={0};CHECK(!pocket_application_open(&app,600,8,script,length));CHECK(!app.impl);free(script);
    }
    /* An event callback cannot mutate native UI while the dispatcher borrows it. */
    const char *tail="\nPocketApplication.event=function(){__pocketCall('layout.invalidate');return 1;};";
    size_t length=source.length+strlen(tail);char *script=malloc(length+1);CHECK(script);
    memcpy(script,source.data,source.length);memcpy(script+source.length,tail,strlen(tail)+1);
    PocketApplication app={0};CHECK(pocket_application_open(&app,600,8,script,length));
    PocketPointerEvent down={.phase=POCKET_POINTER_DOWN,.pointer_id=0,.x=180,.y=476,.timestamp_ms=20};
    PocketPointerEvent up=down;up.phase=POCKET_POINTER_UP;up.timestamp_ms=40;
    CHECK(pocket_interaction_pointer(pocket_application_interaction(&app),&down)==POCKET_INTERACTION_OK);
    (void)pocket_interaction_pointer(pocket_application_interaction(&app),&up);
    CHECK(!pocket_application_step(&app,40));CHECK(pocket_application_error(&app));pocket_application_close(&app);free(script);
    for(unsigned i=0;i<100;i++){
        CHECK(pocket_application_open(&app,i%2?800:600,8,(const char *)source.data,source.length));
        CHECK(pocket_application_step(&app,1));pocket_application_close(&app);CHECK(!app.impl);
    }
    host_asset_free(&source);return 1;
}
int main(int argc,char **argv){
    if(argc!=4&&argc!=5)return 2;
    if(argc==5){
        Driver d;
        if(!open_driver(&d,argv[1],600))return 1;
        /* Deliberately incorrect assertion against real rendered pixels. */
        unsigned observed=d.runtime->frame.pixels[0];
        int unexpectedly_equal=observed==0;
        if(!close_driver(&d))return 1;
        if(unexpectedly_equal)return 0;
        fprintf(stderr,"INTENTIONAL_APPLICATION_ASSERTION_FAILURE blue=%u expected=0\n",observed);
        return 1;
    }
    if(!source_cases(argv[2]))return 1;
    for(unsigned height=600;height<=800;height+=200){
        if(!coffee(argv[1],argv[3],height)||!panel(argv[2],argv[3],height))return 1;
    }
    puts("APPLICATIONS_OK real-quickjs real-core two-interactive-applications privacy lifecycle");return 0;
}
