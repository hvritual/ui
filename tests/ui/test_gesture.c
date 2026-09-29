#include "hosts/linux/ui/interaction.h"
#include "hosts/linux/input/interaction_bridge.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"GESTURE_FAIL %s:%d %s\n",__func__,__LINE__,#x);return 1; } } while(0)
#define OK(x) CHECK((x)==POCKET_INTERACTION_OK)
#define MAX_EVENTS 8192

typedef struct { uint32_t type,id; uint64_t time; int x,y,dx,dy,vx,vy; PocketUiHandle target; } Logged;
typedef struct {
    PocketUiTree tree;PocketLayoutContext layout;PocketComponentRuntime components;
    PocketOverlayManager overlays;PocketInteractionRuntime interaction;
    PocketComponentHandle scene,a,b,field,scroll,child;
    PocketUiHandle root,ah,bh,fh,sh,ch;
    Logged events[MAX_EVENTS];unsigned count;uint64_t now;
    int reenter,return_cancel,destroy_on_up;PocketInteractionStatus reentry_result;
} Fixture;
static PocketUiEventAction observe(void *ctx,PocketUiEvent *e) {
    Fixture *f=ctx;
    if(e->phase!=POCKET_UI_EVENT_TARGET)return POCKET_UI_EVENT_CONTINUE;
    if(f->count<MAX_EVENTS)f->events[f->count++]=(Logged){e->type,e->pointer_id,e->timestamp_ms,e->x,e->y,e->delta_x,e->delta_y,e->velocity_x,e->velocity_y,e->target};
    if(f->reenter && e->type==POCKET_UI_EVENT_POINTER_DOWN) {
        PocketPointerEvent p={999,POCKET_POINTER_DOWN,20,20,e->timestamp_ms};
        f->reentry_result=pocket_interaction_pointer(&f->interaction,&p);
    }
    if(f->destroy_on_up && e->type==POCKET_UI_EVENT_POINTER_UP) {
        (void)pocket_component_destroy(&f->components,f->a);
        f->destroy_on_up=0;
    }
    if(f->return_cancel && e->type==POCKET_UI_EVENT_POINTER_DOWN)return POCKET_UI_EVENT_CANCEL;
    return POCKET_UI_EVENT_CONTINUE;
}
static int add(Fixture *f,PocketComponentKind kind,PocketComponentHandle parent,int x,int y,int w,int h,
               PocketComponentHandle *out,PocketUiHandle *root) {
    CHECK(pocket_component_create(&f->components,kind,parent,NULL,out)==POCKET_COMPONENT_OK);
    PocketLayoutSpec s=pocket_layout_spec_default();s.mode=POCKET_LAYOUT_ABSOLUTE;
    s.width=(PocketLength){POCKET_LENGTH_PX,w};s.height=(PocketLength){POCKET_LENGTH_PX,h};
    s.offset_x=(PocketLength){POCKET_LENGTH_PX,x};s.offset_y=(PocketLength){POCKET_LENGTH_PX,y};
    CHECK(pocket_component_set_layout(&f->components,*out,&s)==POCKET_COMPONENT_OK);
    PocketComponentSnapshot snap;CHECK(pocket_component_snapshot(&f->components,*out,&snap)==POCKET_COMPONENT_OK);
    *root=snap.root;CHECK(pocket_ui_set_event_handler(&f->tree,*root,observe,f)==POCKET_UI_OK);return 0;
}
static int setup(Fixture *f,int height) {
    memset(f,0,sizeof(*f));f->now=1000;
    PocketUiTreeConfig tc={.initial_capacity=32,.update_queue_capacity=32,.update_budget=16};
    CHECK(pocket_ui_tree_init(&f->tree,&tc)==POCKET_UI_OK);
    PocketLayoutConfig lc={.tree=&f->tree,.record_capacity=128};CHECK(pocket_layout_init(&f->layout,&lc)==POCKET_UI_OK);
    PocketComponentRuntimeConfig cc={.tree=&f->tree,.layout=&f->layout,.capacity=128};
    CHECK(pocket_component_runtime_init(&f->components,&cc)==POCKET_COMPONENT_OK);
    PocketOverlayConfig oc={.components=&f->components,.capacity=8};CHECK(pocket_overlay_init(&f->overlays,&oc)==POCKET_OVERLAY_OK);
    CHECK(!add(f,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0,0,1024,height,&f->scene,&f->root));
    CHECK(!add(f,POCKET_COMPONENT_BUTTON,f->scene,0,0,100,80,&f->a,&f->ah));
    CHECK(!add(f,POCKET_COMPONENT_BUTTON,f->scene,924,0,100,80,&f->b,&f->bh));
    CHECK(!add(f,POCKET_COMPONENT_TEXT_FIELD,f->scene,0,height-80,200,80,&f->field,&f->fh));
    CHECK(!add(f,POCKET_COMPONENT_SCROLL,f->scene,200,100,500,300,&f->scroll,&f->sh));
    CHECK(!add(f,POCKET_COMPONENT_BUTTON,f->scroll,0,0,500,100,&f->child,&f->ch));
    CHECK(pocket_layout_run(&f->layout,f->root,1024,height)==POCKET_UI_OK);
    PocketInteractionConfig ic={.tree=&f->tree,.layout=&f->layout,.components=&f->components,.overlays=&f->overlays,.scene_root=f->root};
    OK(pocket_interaction_init(&f->interaction,&ic));return 0;
}
static void teardown(Fixture *f) {
    pocket_interaction_dispose(&f->interaction);pocket_overlay_dispose(&f->overlays);
    pocket_component_runtime_dispose(&f->components);pocket_layout_dispose(&f->layout);pocket_ui_tree_dispose(&f->tree);
}
static PocketInteractionStatus pointer(Fixture *f,uint32_t id,PocketPointerPhase phase,int x,int y,unsigned delta) {
    f->now+=delta;PocketPointerEvent e={id,phase,x,y,f->now};return pocket_interaction_pointer(&f->interaction,&e);
}
static unsigned count(const Fixture *f,uint32_t type,PocketUiHandle h) {
    unsigned result=0;for(unsigned n=0;n<f->count;n++)if(f->events[n].type==type &&
        (!h.slot || (f->events[n].target.slot==h.slot && f->events[n].target.generation==h.generation)))result++;
    return result;
}
static int tick(Fixture *f,unsigned delta) { f->now+=delta;return pocket_interaction_tick(&f->interaction,f->now); }
static int clicks(int height) {
    Fixture f;CHECK(!setup(&f,height));
    OK(pointer(&f,0,POCKET_POINTER_DOWN,0,0,0));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    OK(pointer(&f,0,POCKET_POINTER_UP,0,0,30));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1);
    OK(pointer(&f,1,POCKET_POINTER_DOWN,1023,0,30));OK(pointer(&f,1,POCKET_POINTER_UP,1023,0,30));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.bh)==1);
    OK(pointer(&f,2,POCKET_POINTER_DOWN,20,20,30));OK(pointer(&f,2,POCKET_POINTER_MOVE,960,20,30));
    OK(pointer(&f,2,POCKET_POINTER_UP,960,20,30));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1 && count(&f,POCKET_UI_EVENT_TAP,f.bh)==1);
    CHECK(count(&f,POCKET_UI_EVENT_POINTER_MOVE,f.bh)==0 && count(&f,POCKET_UI_EVENT_POINTER_UP,f.bh)==1);
    OK(pointer(&f,3,POCKET_POINTER_DOWN,20,20,30));OK(pointer(&f,3,POCKET_POINTER_MOVE,60,20,20));
    OK(pointer(&f,3,POCKET_POINTER_MOVE,20,20,20));OK(pointer(&f,3,POCKET_POINTER_UP,20,20,20));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1);
    CHECK(pointer(&f,4,POCKET_POINTER_DOWN,1024,height,20)==POCKET_INTERACTION_NO_TARGET);
    teardown(&f);return 0;
}
static int timed_gestures(void) {
    Fixture f;CHECK(!setup(&f,600));
    OK(pocket_interaction_set_gestures(&f.interaction,f.ah,POCKET_GESTURE_TAP|POCKET_GESTURE_DOUBLE_TAP|POCKET_GESTURE_LONG_PRESS));
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,0));
    CHECK(pocket_interaction_next_deadline(&f.interaction,f.now)==500);
    OK(tick(&f,499));CHECK(count(&f,POCKET_UI_EVENT_LONG_PRESS,f.ah)==0);
    OK(tick(&f,1));OK(tick(&f,1000));CHECK(count(&f,POCKET_UI_EVENT_LONG_PRESS,f.ah)==1);
    OK(pointer(&f,1,POCKET_POINTER_UP,20,20,0));OK(tick(&f,400));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));OK(pointer(&f,1,POCKET_POINTER_UP,20,20,30));
    CHECK(pocket_interaction_next_deadline(&f.interaction,f.now)==300);
    OK(pointer(&f,2,POCKET_POINTER_DOWN,21,21,80));OK(pointer(&f,2,POCKET_POINTER_UP,21,21,30));
    OK(tick(&f,400));CHECK(count(&f,POCKET_UI_EVENT_DOUBLE_TAP,f.ah)==1 && count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));OK(pointer(&f,1,POCKET_POINTER_UP,20,20,30));
    OK(tick(&f,300));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1);
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));OK(pointer(&f,1,POCKET_POINTER_CANCEL,20,20,30));
    OK(tick(&f,900));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1 && count(&f,POCKET_UI_EVENT_LONG_PRESS,f.ah)==1);
    /* Cancel and movement crossing slop win an exact long-press deadline. */
    OK(pointer(&f,8,POCKET_POINTER_DOWN,20,20,10));
    OK(pointer(&f,8,POCKET_POINTER_CANCEL,20,20,500));
    OK(pointer(&f,8,POCKET_POINTER_DOWN,20,20,10));
    OK(pointer(&f,8,POCKET_POINTER_MOVE,80,20,500));
    OK(pointer(&f,8,POCKET_POINTER_UP,80,20,10));
    CHECK(count(&f,POCKET_UI_EVENT_LONG_PRESS,f.ah)==1);
    CHECK(pocket_interaction_next_deadline(&f.interaction,f.now)==UINT32_MAX);
    CHECK(pocket_interaction_tick(&f.interaction,f.now-1)==POCKET_INTERACTION_CLOCK_REVERSED);
    PocketPointerEvent reversed={7,POCKET_POINTER_DOWN,20,20,100};
    CHECK(pocket_interaction_pointer(&f.interaction,&reversed)==POCKET_INTERACTION_CLOCK_REVERSED);
    teardown(&f);return 0;
}
static int scroll_drag_pan(void) {
    Fixture f;CHECK(!setup(&f,600));
    OK(pointer(&f,1,POCKET_POINTER_DOWN,220,120,0));OK(pointer(&f,1,POCKET_POINTER_MOVE,220,160,20));
    OK(pointer(&f,1,POCKET_POINTER_MOVE,220,200,20));OK(pointer(&f,1,POCKET_POINTER_UP,220,200,10));
    CHECK(count(&f,POCKET_UI_EVENT_SCROLL_BEGIN,f.sh)==1 && count(&f,POCKET_UI_EVENT_SCROLL_UPDATE,f.sh)==1 && count(&f,POCKET_UI_EVENT_SCROLL_END,f.sh)==1);
    CHECK(count(&f,POCKET_UI_EVENT_FLICK,f.sh)==1 && count(&f,POCKET_UI_EVENT_TAP,f.ch)==0);
    CHECK(count(&f,POCKET_UI_EVENT_POINTER_CANCEL,f.ch)==1);
    OK(pocket_interaction_set_gestures(&f.interaction,f.ch,POCKET_GESTURE_DRAG|POCKET_GESTURE_TAP));
    OK(pointer(&f,2,POCKET_POINTER_DOWN,220,120,20));OK(pointer(&f,2,POCKET_POINTER_MOVE,220,190,20));
    OK(pointer(&f,2,POCKET_POINTER_UP,220,190,30));CHECK(count(&f,POCKET_UI_EVENT_DRAG_BEGIN,f.ch)==1 && count(&f,POCKET_UI_EVENT_DRAG_END,f.ch)==1);
    CHECK(count(&f,POCKET_UI_EVENT_SCROLL_BEGIN,f.sh)==1);
    OK(pocket_interaction_set_gestures(&f.interaction,f.ah,POCKET_GESTURE_PAN|POCKET_GESTURE_FLICK));
    OK(pointer(&f,3,POCKET_POINTER_DOWN,20,20,20));OK(pointer(&f,3,POCKET_POINTER_MOVE,80,20,20));
    OK(tick(&f,200));OK(pointer(&f,3,POCKET_POINTER_UP,80,20,0));CHECK(count(&f,POCKET_UI_EVENT_PAN_BEGIN,f.ah)==1 && count(&f,POCKET_UI_EVENT_FLICK,f.ah)==0);
    /* Horizontal motion must not let a vertical parent claim the gesture. */
    OK(pocket_interaction_set_gestures(&f.interaction,f.ch,POCKET_GESTURE_TAP));
    OK(pointer(&f,4,POCKET_POINTER_DOWN,220,120,20));OK(pointer(&f,4,POCKET_POINTER_MOVE,300,120,20));OK(pointer(&f,4,POCKET_POINTER_UP,300,120,20));
    CHECK(count(&f,POCKET_UI_EVENT_SCROLL_BEGIN,f.sh)==1 && count(&f,POCKET_UI_EVENT_TAP,f.ch)==0);
    teardown(&f);return 0;
}
static int modal(Fixture *f,uint64_t id,PocketOverlayKind kind,PocketComponentHandle *out,PocketUiHandle *field) {
    PocketUiHandle root;PocketComponentHandle child;
    CHECK(!add(f,kind==POCKET_OVERLAY_MODAL?POCKET_COMPONENT_MODAL:POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0,0,1024,600,out,&root));
    CHECK(!add(f,POCKET_COMPONENT_TEXT_FIELD,*out,0,0,200,80,&child,field));
    CHECK(pocket_layout_run(&f->layout,root,1024,600)==POCKET_UI_OK);
    CHECK(pocket_overlay_present(&f->overlays,&(PocketOverlaySpec){.id=id,.kind=kind,.root=*out,.owns_root=1,.captures_input=1,.captures_focus=1,.focus_token=id})==POCKET_OVERLAY_OK);
    return 0;
}
static int isolation(void) {
    Fixture f;CHECK(!setup(&f,600));OK(pocket_interaction_focus(&f.interaction,f.fh));
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,0));OK(pocket_interaction_capture(&f.interaction,1,f.ah));
    PocketComponentHandle m,k;PocketUiHandle mf,kf;
    CHECK(!modal(&f,10,POCKET_OVERLAY_MODAL,&m,&mf));OK(tick(&f,10));
    CHECK(count(&f,POCKET_UI_EVENT_POINTER_CANCEL,f.ah)==1);
    OK(pointer(&f,1,POCKET_POINTER_MOVE,20,20,10));OK(pointer(&f,1,POCKET_POINTER_UP,20,20,10));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0 && count(&f,POCKET_UI_EVENT_POINTER_UP,mf)==0);
    OK(pocket_interaction_focus(&f.interaction,mf));CHECK(!modal(&f,11,POCKET_OVERLAY_KEYBOARD,&k,&kf));
    OK(tick(&f,10));OK(pocket_interaction_focus(&f.interaction,kf));
    CHECK(pocket_overlay_dismiss(&f.overlays,11)==POCKET_OVERLAY_OK);OK(tick(&f,10));
    PocketInteractionSnapshot s;OK(pocket_interaction_snapshot(&f.interaction,&s));CHECK(s.focused.slot==mf.slot && s.focused.generation==mf.generation);
    CHECK(pocket_overlay_dismiss(&f.overlays,10)==POCKET_OVERLAY_OK);OK(tick(&f,10));
    OK(pocket_interaction_snapshot(&f.interaction,&s));CHECK(s.focused.slot==f.fh.slot && s.focused.generation==f.fh.generation);
    /* Pending single taps must be cancelled when a keyboard opens. */
    OK(pocket_interaction_set_gestures(&f.interaction,f.ah,POCKET_GESTURE_TAP|POCKET_GESTURE_DOUBLE_TAP));
    OK(pointer(&f,2,POCKET_POINTER_DOWN,20,20,10));OK(pointer(&f,2,POCKET_POINTER_UP,20,20,10));
    CHECK(!modal(&f,12,POCKET_OVERLAY_KEYBOARD,&k,&kf));OK(tick(&f,400));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    CHECK(pocket_overlay_dismiss(&f.overlays,12)==POCKET_OVERLAY_OK);OK(tick(&f,10));
    /* An overlay opened and dismissed within one frame still revokes a gesture. */
    OK(pointer(&f,3,POCKET_POINTER_DOWN,20,20,10));
    CHECK(!modal(&f,13,POCKET_OVERLAY_MODAL,&m,&mf));
    CHECK(pocket_interaction_capture(&f.interaction,3,mf)==POCKET_INTERACTION_CAPTURE_ERROR);
    CHECK(pocket_overlay_dismiss(&f.overlays,13)==POCKET_OVERLAY_OK);
    OK(pointer(&f,3,POCKET_POINTER_UP,20,20,10));OK(tick(&f,400));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    teardown(&f);return 0;
}
static int safety(void) {
    Fixture f;CHECK(!setup(&f,600));
    f.reenter=1;OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,0));CHECK(f.reentry_result==POCKET_INTERACTION_BUSY);
    OK(pointer(&f,1,POCKET_POINTER_CANCEL,20,20,10));f.reenter=0;
    f.return_cancel=1;OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));OK(pointer(&f,1,POCKET_POINTER_UP,20,20,10));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);f.return_cancel=0;
    OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));CHECK(pocket_component_set_disabled(&f.components,f.scene,1)==POCKET_COMPONENT_OK);
    OK(tick(&f,10));OK(pointer(&f,1,POCKET_POINTER_UP,20,20,10));CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    CHECK(pocket_component_set_disabled(&f.components,f.scene,0)==POCKET_COMPONENT_OK);
    for(unsigned n=0;n<8;n++)OK(pointer(&f,n,POCKET_POINTER_DOWN,20,20,10));
    CHECK(pointer(&f,9,POCKET_POINTER_DOWN,20,20,10)==POCKET_INTERACTION_POINTER_BUSY);
    pocket_interaction_cancel_all(&f.interaction,f.now);PocketInteractionSnapshot s;OK(pocket_interaction_snapshot(&f.interaction,&s));CHECK(s.active_pointers==0);
    f.destroy_on_up=1;OK(pointer(&f,1,POCKET_POINTER_DOWN,20,20,10));
    PocketInteractionStatus status=pointer(&f,1,POCKET_POINTER_UP,20,20,10);
    CHECK(status==POCKET_INTERACTION_OK || status==POCKET_INTERACTION_STALE_HANDLE);CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    teardown(&f);return 0;
}
static int malformed_bridge(void) {
    Fixture f;CHECK(!setup(&f,600));PocketInputInteractionBridge b;
    CHECK(pocket_input_interaction_bridge_init(&b,&f.interaction));
    InputFrame frame={0};frame.contact_count=1;frame.contacts[0]=(InputContact){0,20,20};
    CHECK(pocket_input_interaction_bridge_frame(&b,&frame,1000000000ULL));
    frame.contact_count=2;frame.contacts[1]=frame.contacts[0];
    CHECK(!pocket_input_interaction_bridge_frame(&b,&frame,1010000000ULL));
    CHECK(count(&f,POCKET_UI_EVENT_POINTER_CANCEL,f.ah)==1 && b.count==0);
    frame.contact_count=1;frame.contacts[0].id=1;
    CHECK(pocket_input_interaction_bridge_frame(&b,&frame,1020000000ULL));
    pocket_interaction_cancel_all(&f.interaction,1030);
    CHECK(pocket_input_interaction_bridge_frame(&b,&frame,1040000000ULL));
    frame.contact_count=0;CHECK(pocket_input_interaction_bridge_frame(&b,&frame,1050000000ULL));
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==0);
    teardown(&f);return 0;
}
static int reuse_budget(void) {
    Fixture f;CHECK(!setup(&f,600));
    for(unsigned n=0;n<1200;n++) {
        PocketComponentHandle c;PocketUiHandle h;
        CHECK(!add(&f,POCKET_COMPONENT_BUTTON,f.scene,800,400,100,80,&c,&h));
        OK(pocket_interaction_set_gestures(&f.interaction,h,POCKET_GESTURE_TAP));
        CHECK(pocket_component_destroy(&f.components,c)==POCKET_COMPONENT_OK);
        CHECK(pocket_interaction_focus(&f.interaction,h)==POCKET_INTERACTION_STALE_HANDLE);
    }
    CHECK(pocket_component_live_count(&f.components)==6);
    teardown(&f);return 0;
}
static int replay(const char *path,int height) {
    Fixture f;CHECK(!setup(&f,height));
    FILE *file=fopen(path,"r");CHECK(file!=NULL);char line[192];unsigned rows=0;
    while(fgets(line,sizeof(line),file)) {
        if(line[0]=='#')continue;
        uint64_t ms;unsigned phase,id;int x,y;char extra;
        CHECK(sscanf(line,"%" SCNu64 ",%u,%u,%d,%d %c",&ms,&phase,&id,&x,&y,&extra)==5);
        PocketPointerEvent e={id,(PocketPointerPhase)phase,x,y,ms};
        PocketInteractionStatus s=pocket_interaction_pointer(&f.interaction,&e);
        CHECK(s==POCKET_INTERACTION_OK || s==POCKET_INTERACTION_NO_TARGET);rows++;
    }
    CHECK(!ferror(file));CHECK(!fclose(file));CHECK(rows>0);
    for(unsigned n=0;n<f.count;n++) {
        Logged *e=&f.events[n];
        printf("%u,%u,%" PRIu64 ",%u:%u,%d,%d,%d,%d,%d,%d\n",e->type,e->id,e->time,e->target.slot,e->target.generation,e->x,e->y,e->dx,e->dy,e->vx,e->vy);
    }
    CHECK(count(&f,POCKET_UI_EVENT_TAP,f.ah)==1 && count(&f,POCKET_UI_EVENT_TAP,f.bh)==1);
    CHECK(count(&f,POCKET_UI_EVENT_SCROLL_BEGIN,f.sh)==1 && count(&f,POCKET_UI_EVENT_TAP,f.ch)==0);
    teardown(&f);return 0;
}
int main(int argc,char **argv) {
    if(argc==4 && !strcmp(argv[1],"--replay")) { CHECK(!replay(argv[2],atoi(argv[3])));puts("REPLAY_OK");return 0; }
    if(argc==2 && !strcmp(argv[1],"--intentional-failure")) {
        Fixture f;CHECK(!setup(&f,600));OK(pointer(&f,0,POCKET_POINTER_DOWN,20,20,0));
        OK(pointer(&f,0,POCKET_POINTER_UP,20,20,10));
        unsigned observed=count(&f,POCKET_UI_EVENT_TAP,f.ah);teardown(&f);
        CHECK(observed==0);return 0;
    }
    CHECK(argc==1);CHECK(!clicks(600));CHECK(!clicks(800));CHECK(!timed_gestures());CHECK(!scroll_drag_pan());
    CHECK(!isolation());CHECK(!safety());CHECK(!malformed_bridge());CHECK(!reuse_budget());
    puts("GESTURE_OK suites=8 lifecycle=1200");return 0;
}
