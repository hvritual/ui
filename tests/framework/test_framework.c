#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/framework.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* All interaction uses the production InputFrame -> F6 -> application path.
 * The display callback observes real PocketJS pixels, never fabricates them. */
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FRAMEWORK_FAIL line=%d expr=%s\n",__LINE__,#x);return 0;} } while(0)
typedef struct {PocketFramework *r;uint64_t ms,seq;const char *out;unsigned height;FILE *trace;} Driver;
static uint64_t hash_frame(const PocketEngineFrame *f){
    uint64_t h=14695981039346656037ULL;
    for(size_t i=0;i<f->length;i++){h^=f->pixels[i];h*=1099511628211ULL;}
    return h;
}
static int mark(Driver *d,const char *name){
    CoffeeAppStats s;CHECK(coffee_app_stats(&d->r->app,&s));
    CHECK(fprintf(d->trace,"%s h=%u page=%u selected=%u first=%u locale=%u theme=%u progress=%u completed=%u modal=%u nodes=%u pool=%u hash=%016llx\n",name,d->height,s.page,s.selected,s.first,s.locale,s.theme,s.progress,s.completed,s.modal,s.nodes,s.pool,(unsigned long long)hash_frame(&d->r->frame))>0);
    return 1;
}
static int step(Driver *d,unsigned ms,int force){d->ms+=ms;CHECK(pocket_framework_tick(d->r,d->ms*1000000ULL,force));return 1;}
static int contact(Driver *d,int down,int x,int y){
    InputFrame f={0};f.sequence=++d->seq;
    if(down){f.contact_count=1;f.contacts[0]=(InputContact){0,x,y};}
    d->ms+=20;CHECK(pocket_framework_input(d->r,&f,d->ms*1000000ULL));CHECK(step(d,1,1));return 1;
}
static int tap(Driver *d,int x,int y){CHECK(contact(d,1,x,y));CHECK(contact(d,0,x,y));return 1;}
static int state(Driver *d,unsigned page,unsigned modal){CoffeeAppStats s;CHECK(coffee_app_stats(&d->r->app,&s));CHECK(s.page==page&&s.modal==modal);CHECK(s.pool<=6&&s.peak_pool<=6&&s.nodes<=64);return 1;}
static int shot(Driver *d,const char *name){char path[4096];CHECK(snprintf(path,sizeof(path),"%s/%s-%u.ppm",d->out,name,d->height)<(int)sizeof(path));CHECK(pocket_framework_snapshot(d->r,path));CHECK(mark(d,name));return 1;}
static int begin(Driver *d,const char *assets,const char *out,unsigned height,unsigned items,const char *store){
    memset(d,0,sizeof(*d));d->r=calloc(1,sizeof(*d->r));CHECK(d->r);d->ms=1000;d->height=height;d->out=out;
    char path[4096];CHECK(snprintf(path,sizeof(path),"%s/replay-%u-%u.txt",out,height,items)<(int)sizeof(path));d->trace=fopen(path,"wx");CHECK(d->trace);
    CHECK(pocket_framework_open(d->r,height,items,assets,store,NULL));CHECK(step(d,0,1));return 1;
}
static int end(Driver *d){
    CHECK(pocket_framework_close(d->r));CHECK(host_alloc_stats().live_bytes==0);
    CHECK(fclose(d->trace)==0);free(d->r);return 1;
}
static int write_update(const char *store,char id,const char *source,int corrupt){
    char name[4096],gen[65];memset(gen,id,64);gen[64]=0;
    CHECK(snprintf(name,sizeof(name),"%s/%s.rgba",store,gen)<(int)sizeof(name));
    FILE *in=fopen(source,"rb"),*out=fopen(name,"wb");CHECK(in&&out);int c;size_t n=0;
    while((c=fgetc(in))!=EOF){if(corrupt&&n==100)c^=1;CHECK(fputc(c,out)!=EOF);n++;}
    CHECK(!ferror(in)&&fclose(in)==0&&fclose(out)==0);
    CHECK(snprintf(name,sizeof(name),"%s/current",store)<(int)sizeof(name));out=fopen(name,"wb");CHECK(out);
    CHECK(fprintf(out,"%s\n-\n",gen)==67);CHECK(fclose(out)==0);return 1;
}
static int invalid_scene(Driver *d){
    PocketScene *s=malloc(sizeof(*s));CHECK(s);*s=d->r->scene;
    PocketEngineResource h;uint64_t before=hash_frame(&d->r->frame),uploads=d->r->engine.scene_uploads;
    s->records[1].id=s->records[0].id;
    CHECK(pocket_scene_engine_api.resource_create(&d->r->engine,POCKET_SCENE_RESOURCE,s,sizeof(*s),&h)==POCKET_ENGINE_INVALID_ARGUMENT);
    *s=d->r->scene;s->records[0].resource_ref=9;
    CHECK(pocket_scene_engine_api.resource_create(&d->r->engine,POCKET_SCENE_RESOURCE,s,sizeof(*s),&h)==POCKET_ENGINE_INVALID_ARGUMENT);
    *s=d->r->scene;s->count=POCKET_SCENE_MAX_RECORDS+1;
    CHECK(pocket_scene_engine_api.resource_create(&d->r->engine,POCKET_SCENE_RESOURCE,s,sizeof(*s),&h)==POCKET_ENGINE_INVALID_ARGUMENT);
    CHECK(pocket_scene_engine_api.resource_release(&d->r->engine,(PocketEngineResource){1,0})==POCKET_ENGINE_STALE_HANDLE);
    CHECK(step(d,17,1));CHECK(hash_frame(&d->r->frame)==before&&d->r->engine.scene_uploads==uploads);free(s);return 1;
}
/* Packet protocol integration uses the production state parser, F6 bridge and
 * real renderer. Synthetic axes here are not this board's calibration. */
static int raw_a_tap(Driver *d,InputState *raw,int x,int y) {
    CHECK(input_state_feed(raw,EV_KEY,BTN_TOUCH,1)==1);
    CHECK(input_state_feed(raw,EV_ABS,ABS_MT_TRACKING_ID,0)==1);
    CHECK(input_state_feed(raw,EV_ABS,ABS_MT_POSITION_X,x)==1);
    CHECK(input_state_feed(raw,EV_ABS,ABS_MT_POSITION_Y,y)==1);
    CHECK(input_state_feed(raw,EV_SYN,SYN_MT_REPORT,0)==1);
    CHECK(input_state_feed(raw,EV_SYN,SYN_REPORT,0)==2);
    d->ms+=20;CHECK(pocket_framework_input(d->r,input_state_frame(raw),d->ms*1000000ULL));
    CHECK(step(d,1,1));
    CHECK(input_state_feed(raw,EV_KEY,BTN_TOUCH,0)==1);
    CHECK(input_state_feed(raw,EV_SYN,SYN_REPORT,0)==2);
    d->ms+=20;CHECK(pocket_framework_input(d->r,input_state_frame(raw),d->ms*1000000ULL));
    CHECK(step(d,1,1));return 1;
}
static int raw_a_flow(Driver *d) {
    InputState raw;
    InputTransform t={.x={0,1023},.y={0,(int)d->height-1},.width=1024,.height=d->height};
    CHECK(input_state_init(&raw,INPUT_PROTOCOL_MT_A,INPUT_HW_MAX_SLOTS,&t));
    CHECK(raw_a_tap(d,&raw,100,200));CHECK(state(d,2,0));
    CHECK(raw_a_tap(d,&raw,640,(int)d->height-112));CHECK(state(d,2,1));
    CHECK(raw_a_tap(d,&raw,330,(int)d->height-112));CHECK(state(d,2,1));
    CHECK(raw_a_tap(d,&raw,640,364));CHECK(state(d,3,0));
    CHECK(step(d,5001,1));CHECK(state(d,4,0));
    CHECK(raw_a_tap(d,&raw,500,(int)d->height-91));CHECK(state(d,1,0));
    CHECK(mark(d,"raw-mt-a-full-ui"));return 1;
}
static int flow(const char *assets,const char *out,unsigned height,int wrong_pixel){
    Driver d;char store[4096],a[4096],b[4096];
    CHECK(snprintf(store,sizeof(store),"%s/store-%u",out,height)<(int)sizeof(store));CHECK(mkdir(store,0700)==0);
    CHECK(snprintf(a,sizeof(a),"%s/builtin.rgba",assets)<(int)sizeof(a));CHECK(snprintf(b,sizeof(b),"%s/alternate.rgba",assets)<(int)sizeof(b));
    CHECK(begin(&d,assets,out,height,8,store));
    unsigned expected_blue=wrong_pixel?0U:240U;unsigned actual_blue=d.r->frame.pixels[0];
    if(actual_blue!=expected_blue){
        fprintf(stderr,"INTENTIONAL_ASSERTION_FAILURE actual_blue=%u expected_blue=%u\n",actual_blue,expected_blue);
        CHECK(end(&d));return 0;
    }
    CHECK(shot(&d,"home"));
    uint64_t home=hash_frame(&d.r->frame),presents=d.r->presents,uploads=d.r->engine.scene_uploads;
    for(unsigned i=0;i<120;i++)CHECK(step(&d,17,0));
    CHECK(d.r->presents==presents&&d.r->engine.scene_uploads==uploads&&d.r->clean_skips>=60);
    CHECK(invalid_scene(&d));CHECK(mark(&d,"idle-and-reject"));

    /* Theme/locale preserve model and native handles; framebuffer truly changes. */
    CHECK(tap(&d,730,48));CHECK(hash_frame(&d.r->frame)!=home);CHECK(shot(&d,"dark"));
    CHECK(tap(&d,730,48));CHECK(hash_frame(&d.r->frame)==home);
    for(unsigned i=0;i<6;i++){CHECK(tap(&d,890,48));CoffeeAppStats s;CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.locale==(i+1)%6);CHECK(mark(&d,"locale"));}
    CHECK(hash_frame(&d.r->frame)==home);

    /* Drag across rows must not be reinterpreted as a drink selection. */
    CHECK(contact(&d,1,100,200));CHECK(contact(&d,1,100,(int)height-160));CHECK(contact(&d,0,100,(int)height-160));CHECK(state(&d,1,0));
    CHECK(tap(&d,870,(int)height-36));CoffeeAppStats s;CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==6&&s.pool==6);CHECK(shot(&d,"last-page"));
    CHECK(tap(&d,100,200));CHECK(state(&d,2,0));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.selected==6);CHECK(shot(&d,"detail"));
    CHECK(tap(&d,330,(int)height-112));CHECK(state(&d,1,0));
    CHECK(tap(&d,870,(int)height-36));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==0);

    /* Simultaneous releases queue at most one navigation, not a host failure. */
    InputFrame multi={.contact_count=2,.contacts={{0,100,200},{1,430,200}}};
    d.ms+=20;CHECK(pocket_framework_input(d.r,&multi,d.ms*1000000ULL));CHECK(step(&d,1,1));
    multi.contact_count=0;d.ms+=20;CHECK(pocket_framework_input(d.r,&multi,d.ms*1000000ULL));CHECK(step(&d,1,1));
    CHECK(state(&d,2,0));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.selected==0);
    CHECK(tap(&d,330,(int)height-112));CHECK(state(&d,1,0));CHECK(mark(&d,"simultaneous-tap"));

    /* Paging while another finger holds a recycled card cannot select its new key. */
    multi=(InputFrame){.contact_count=2,.contacts={{0,100,200},{1,870,(int)height-36}}};
    d.ms+=20;CHECK(pocket_framework_input(d.r,&multi,d.ms*1000000ULL));CHECK(step(&d,1,1));
    multi.contact_count=1;d.ms+=20;CHECK(pocket_framework_input(d.r,&multi,d.ms*1000000ULL));CHECK(step(&d,1,1));
    CHECK(state(&d,1,0));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==6);
    CHECK(contact(&d,0,100,200));CHECK(state(&d,1,0));
    CHECK(tap(&d,870,(int)height-36));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==0);CHECK(mark(&d,"recycle-held-pointer"));

    /* Backend cancellation consumes the old down; late release is not a tap. */
    CHECK(contact(&d,1,100,200));InputFrame lost={.syn_dropped=1,.suppressed=1,.sequence=++d.seq};
    d.ms+=20;CHECK(pocket_framework_input(d.r,&lost,d.ms*1000000ULL));CHECK(contact(&d,0,100,200));CHECK(state(&d,1,0));
    CHECK(contact(&d,1,100,200));d.ms+=20;pocket_framework_disconnect(d.r,d.ms*1000000ULL);CHECK(contact(&d,0,100,200));CHECK(state(&d,1,0));
    PocketInteractionSnapshot input;CHECK(pocket_interaction_snapshot(coffee_app_interaction(&d.r->app),&input)==POCKET_INTERACTION_OK&&input.active_pointers==0);
    CHECK(mark(&d,"cancel-recover"));

    CHECK(tap(&d,100,200));CHECK(state(&d,2,0));CHECK(tap(&d,640,(int)height-112));CHECK(state(&d,2,1));CHECK(shot(&d,"modal"));
    CHECK(tap(&d,330,(int)height-112));CHECK(state(&d,2,1)); /* background Back blocked */
    CHECK(tap(&d,380,364));CHECK(state(&d,2,0));CHECK(tap(&d,640,(int)height-112));CHECK(state(&d,2,1));
    CHECK(write_update(store,'b',b,0));CHECK(step(&d,501,1));CHECK(d.r->media.applied_count==0&&d.r->media.deferred_count>0);
    CHECK(tap(&d,640,364));CHECK(state(&d,3,0));CHECK(shot(&d,"making-start"));
    uint64_t started=d.ms-1;CHECK(step(&d,2500,1));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.progress>=50&&s.progress<=51);CHECK(shot(&d,"making-half"));
    /* Independent pixel oracle: progress bar filled left, not right, in same frame. */
    const uint8_t *left=d.r->frame.pixels+392*d.r->frame.stride+300*4;
    const uint8_t *right=d.r->frame.pixels+392*d.r->frame.stride+700*4;
    CHECK(memcmp(left,right,3)!=0);
    CHECK(step(&d,(unsigned)(started+5001-d.ms),1));CHECK(state(&d,4,0));CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.completed==1);CHECK(shot(&d,"success"));
    CHECK(tap(&d,500,(int)height-91));CHECK(state(&d,1,0));CHECK(step(&d,501,1));CHECK(d.r->media.applied_count==1);CHECK(hash_frame(&d.r->frame)!=home);CHECK(shot(&d,"media-b"));
    uint64_t updated=hash_frame(&d.r->frame);
    CHECK(write_update(store,'c',a,1));CHECK(step(&d,501,1));CHECK(d.r->media.rejected_count==1&&hash_frame(&d.r->frame)==updated);
    CHECK(step(&d,501,1));CHECK(d.r->media.rejected_count==1);
    CHECK(write_update(store,'a',a,0));CHECK(step(&d,501,1));CHECK(d.r->media.applied_count==2&&hash_frame(&d.r->frame)==home);CHECK(mark(&d,"media-reject-rollback"));

    /* Real renderer/navigation churn, binding cleanup and cancel mid-progress. */
    for(unsigned i=0;i<24;i++){
        CHECK(tap(&d,100,200));CHECK(tap(&d,640,(int)height-112));CHECK(tap(&d,640,364));CHECK(state(&d,3,0));
        CHECK(step(&d,250,1));CHECK(tap(&d,500,(int)height-91));CHECK(state(&d,1,0));
    }
    CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.nodes==26&&s.pool==6&&s.completed==1);
    CHECK(d.r->page_mask==15&&d.r->modal_seen==1);CHECK(mark(&d,"lifecycle-24"));CHECK(raw_a_flow(&d));CHECK(end(&d));return 1;
}
static int stress(const char *assets,const char *out,unsigned height){
    Driver d;CHECK(begin(&d,assets,out,height,100,NULL));
    for(unsigned i=0;i<17;i++){
        CoffeeAppStats s;CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==(i*6)%102&&s.nodes==26&&s.pool==6);CHECK(mark(&d,"virtual-window"));
        CHECK(tap(&d,870,(int)height-36));
    }
    CoffeeAppStats s;CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==0&&s.recycled>=94);
    CHECK(contact(&d,1,800,200));CHECK(contact(&d,1,620,200));CHECK(contact(&d,1,450,200));CHECK(contact(&d,0,450,200));
    CHECK(coffee_app_stats(&d.r->app,&s));CHECK(s.first==6&&s.page==1);CHECK(mark(&d,"scroll-arbitration"));CHECK(end(&d));return 1;
}
static PocketEngineStatus reject_present(void *p,const PocketEngineFrame *f){unsigned *n=p;(*n)++;return f&&f->pixels?POCKET_ENGINE_BACKEND_FAILED:POCKET_ENGINE_INVALID_ARGUMENT;}
static int failures(const char *assets){
    PocketFramework *r=calloc(1,sizeof(*r));CHECK(r);unsigned calls=0;
    PocketDisplayBackend display={.abi_major=1,.struct_size=sizeof(display),.context=&calls,.present=reject_present};
    CHECK(pocket_framework_open(r,600,8,assets,NULL,&display));CHECK(!pocket_framework_tick(r,1,1));CHECK(calls==1&&!strcmp(r->error,"DISPLAY_PRESENT"));CHECK(pocket_framework_close(r));CHECK(host_alloc_stats().live_bytes==0);free(r);
    r=calloc(1,sizeof(*r));CHECK(r);CHECK(pocket_framework_open(r,600,8,assets,NULL,NULL));CHECK(pocket_framework_tick(r,1000000,1));
    InputFrame invalid={.contact_count=2,.contacts={{1,20,20},{1,100,100}}};CHECK(!pocket_framework_input(r,&invalid,2000000));CHECK(!strcmp(r->error,"INPUT_FRAME_REJECTED"));CHECK(r->input.count==0);CHECK(pocket_framework_close(r));free(r);CHECK(host_alloc_stats().live_bytes==0);return 1;
}
int main(int argc,char **argv){
    if(argc<3||argc>4)return 2;
    for(unsigned h=600;h<=800;h+=200)if(!flow(argv[1],argv[2],h,argc==4)||!stress(argv[1],argv[2],h))return 1;
    if(!failures(argv[1]))return 1;
    puts("FRAMEWORK_OK dual-viewport real-core navigation modal reactive model input assets cleanup");return 0;
}
