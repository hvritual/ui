#include "input/state.h"
#include <linux/input.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); ++failures; return; } } while(0)
#define PASS(x) printf("PASS %s\n",x)

static InputTransform tf(unsigned w, unsigned h) {
    InputTransform t = {
        .x = {.minimum=0,.maximum=16384},
        .y = {.minimum=0,.maximum=16384},
        .width=w,.height=h
    };
    return t;
}
static int feed(InputState *s, unsigned t, unsigned c, int v) {
    return input_state_feed(s,(uint16_t)t,(uint16_t)c,v);
}
static void down(InputState *s,int slot,int tracking,int x,int y) {
    CHECK(feed(s,EV_ABS,ABS_MT_SLOT,slot));
    CHECK(feed(s,EV_ABS,ABS_MT_TRACKING_ID,tracking));
    CHECK(feed(s,EV_ABS,ABS_MT_POSITION_X,x));
    CHECK(feed(s,EV_ABS,ABS_MT_POSITION_Y,y));
}
static void up(InputState *s,int slot) {
    CHECK(feed(s,EV_ABS,ABS_MT_SLOT,slot));
    CHECK(feed(s,EV_ABS,ABS_MT_TRACKING_ID,-1));
}
static void report(InputState *s) { CHECK(feed(s,EV_SYN,SYN_REPORT,0)==2); }

static void mapping(void) {
    InputTransform t=tf(1024,600); int x=-1,y=-1;
    CHECK(input_map_point(&t,0,0,&x,&y)&&x==0&&y==0);
    CHECK(input_map_point(&t,16384,16384,&x,&y)&&x==1023&&y==599);
    CHECK(input_map_point(&t,8192,8192,&x,&y)&&x==512&&y==300);
    CHECK(input_map_point(&t,-100,20000,&x,&y)&&x==0&&y==599);
    t.invert_x=1;t.invert_y=1;
    CHECK(input_map_point(&t,0,0,&x,&y)&&x==1023&&y==599);
    t.invert_x=t.invert_y=0;t.swap_xy=1;t.width=600;t.height=1024;
    CHECK(input_map_point(&t,16384,0,&x,&y)&&x==0&&y==1023);
    PASS("coordinate-map-round-clamp-invert-swap");
}
static void tap_drag_release(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,0,77,0,0); report(&s);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==0&&f->contacts[0].x==0&&f->contacts[0].y==0);
    CHECK(feed(&s,EV_ABS,ABS_MT_SLOT,0));
    CHECK(feed(&s,EV_ABS,ABS_MT_POSITION_X,8192));
    CHECK(feed(&s,EV_ABS,ABS_MT_POSITION_Y,8192)); report(&s);
    f=input_state_frame(&s); CHECK(f->contact_count==1&&f->contacts[0].x==512&&f->contacts[0].y==300);
    up(&s,0); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==0&&f->cancelled_count==0&&!f->suppressed);
    PASS("mtb-tap-drag-normal-release");
}
static void multi_slot(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,3,103,1000,2000); down(&s,7,107,12000,14000); report(&s);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==2&&f->contacts[0].id==3&&f->contacts[1].id==7);
    up(&s,3); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==7&&f->cancelled_count==0);
    PASS("mtb-multi-slot-stable-id");
}
static void overflow(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    for(int i=0;i<8;i++) down(&s,i,100+i,1000+i*100,2000+i*100);
    report(&s); const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==8&&!f->suppressed);
    down(&s,8,108,9000,9000); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==0&&f->cancelled_count==8&&f->suppressed);
    for(int i=0;i<9;i++) up(&s,i);
    report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==0&&!f->suppressed);
    down(&s,9,109,16384,16384); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==9);
    PASS("mtb-overflow-cancel-suppress-until-all-up");
}
static void dropped_resync(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,0,1,1000,1000); down(&s,1,2,2000,2000); report(&s);
    CHECK(feed(&s,EV_SYN,SYN_DROPPED,0)==2);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==0&&f->cancelled_count==2&&f->syn_dropped&&f->suppressed);
    CHECK(feed(&s,EV_ABS,ABS_MT_SLOT,2)==1);
    CHECK(feed(&s,EV_SYN,SYN_REPORT,0)==3);
    InputMtSnapshot snap={.slot_count=10};
    for(unsigned i=0;i<10;i++) snap.tracking_id[i]=-1;
    snap.tracking_id[2]=22;snap.raw_x[2]=5000;snap.raw_y[2]=6000;snap.have_position[2]=1;
    CHECK(input_state_resync_mt(&s,&snap));
    CHECK(feed(&s,EV_ABS,ABS_MT_SLOT,2));
    CHECK(feed(&s,EV_ABS,ABS_MT_TRACKING_ID,-1));
    report(&s); f=input_state_frame(&s); CHECK(!f->suppressed&&f->contact_count==0);
    down(&s,2,23,5000,6000); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==2);
    PASS("syn-dropped-cancel-resync-all-up-gate");
}
static void disconnect_case(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,4,44,100,200); report(&s);
    input_state_disconnect(&s);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==0&&f->cancelled_count==1&&f->cancelled[0]==4&&f->suppressed);
    CHECK(!feed(&s,EV_SYN,SYN_REPORT,0));
    PASS("disconnect-cancels-published");
}
static void legacy(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_SINGLE,0,&t));
    CHECK(feed(&s,EV_ABS,ABS_X,16384)); CHECK(feed(&s,EV_ABS,ABS_Y,0));
    CHECK(feed(&s,EV_KEY,BTN_TOUCH,1)); report(&s);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==0&&f->contacts[0].x==1023&&f->contacts[0].y==0);
    CHECK(feed(&s,EV_KEY,BTN_TOUCH,0)); report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==0);
    PASS("legacy-single-touch-fallback");
}

static void pending_release_drop(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,0,1,1000,1000); report(&s);
    up(&s,0);
    CHECK(feed(&s,EV_SYN,SYN_DROPPED,0)==2);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->cancelled_count==1&&f->cancelled[0]==0&&f->syn_dropped);
    PASS("pending-release-before-syn-dropped-still-cancels");
}
static void tracking_replacement(void) {
    InputState s; InputTransform t=tf(1024,600);
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    down(&s,0,1,1000,1000); report(&s);
    CHECK(feed(&s,EV_ABS,ABS_MT_SLOT,0));
    CHECK(feed(&s,EV_ABS,ABS_MT_TRACKING_ID,2));
    CHECK(feed(&s,EV_ABS,ABS_MT_POSITION_X,2000));
    CHECK(feed(&s,EV_ABS,ABS_MT_POSITION_Y,3000));
    report(&s);
    const InputFrame *f=input_state_frame(&s);
    CHECK(f->contact_count==0&&f->cancelled_count==1&&f->cancelled[0]==0);
    report(&s); f=input_state_frame(&s);
    CHECK(f->contact_count==1&&f->contacts[0].id==0);
    PASS("tracking-id-replacement-cancel-then-rearm");
}
static void invalids(void) {
    InputState s; InputTransform t=tf(1024,600), bad=t;
    bad.x.maximum=0;
    CHECK(!input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&bad));
    CHECK(!input_state_init(&s,INPUT_PROTOCOL_MT_B,0,&t));
    CHECK(input_state_init(&s,INPUT_PROTOCOL_MT_B,10,&t));
    CHECK(!feed(&s,EV_ABS,ABS_MT_SLOT,10));
    CHECK(!feed(&s,EV_ABS,ABS_MT_SLOT,-1));
    PASS("invalid-config-and-slot-fail-closed");
}
int main(void) {
    mapping(); tap_drag_release(); multi_slot(); overflow(); dropped_resync();
    disconnect_case(); legacy(); pending_release_drop(); tracking_replacement(); invalids();
    if(failures){fprintf(stderr,"INPUT_STATE_FAILED failures=%d\n",failures);return 1;}
    printf("INPUT_STATE_OK cases=10 hardware_trace_required=true\n");return 0;
}
