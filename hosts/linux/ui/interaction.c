#include "interaction.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 256U
#define HIT_BUDGET 4096U
#define FOCUS_HISTORY 16U
#define PENDING_TAPS 16U

typedef struct {
    int active, suppressed, moved, long_sent, prevented;
    uint32_t id, tap_mask, motion_event;
    PocketUiHandle down_target, capture, tap_target, motion_target, root;
    int32_t x, y, start_x, start_y, velocity_x, velocity_y;
    uint64_t down_ms, sample_ms, velocity_ms;
} PointerSlot;
typedef struct { PocketUiHandle target; uint32_t mask; } GestureBinding;
typedef struct { PocketUiHandle root, focused, scope; } FocusRecord;
typedef struct { int active; PointerSlot pointer; uint64_t due; } PendingTap;
typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketComponentRuntime *components;
    PocketOverlayManager *overlays;
    PocketUiHandle scene_root, current_root, focus_scope, focused;
    PointerSlot pointers[POCKET_INTERACTION_MAX_POINTERS];
    GestureBinding bindings[POCKET_GESTURE_MAX_BINDINGS];
    FocusRecord history[FOCUS_HISTORY];
    PendingTap pending[PENDING_TAPS];
    uint64_t clock_ms, last_input_ms, overlay_epoch;
    unsigned dispatching;
    int processing, focus_changing, cancel_requested;
} InteractionImpl;

static InteractionImpl *ii(PocketInteractionRuntime *r) { return r ? r->impl : NULL; }
static const InteractionImpl *cii(const PocketInteractionRuntime *r) { return r ? r->impl : NULL; }
static int equal(PocketUiHandle a, PocketUiHandle b) { return a.slot==b.slot && a.generation==b.generation; }
static int live(InteractionImpl *i, PocketUiHandle h) {
    PocketUiSnapshot s;
    return pocket_ui_snapshot(i->tree,h,&s)==POCKET_UI_OK;
}
static int mounted(PocketUiLifecyclePhase p) {
    return p==POCKET_UI_PHASE_MOUNTED || p==POCKET_UI_PHASE_UPDATED ||
           p==POCKET_UI_PHASE_LAYOUT || p==POCKET_UI_PHASE_PAINT;
}
static int within(PocketUiRect r,int32_t x,int32_t y) {
    return r.width>0 && r.height>0 && x>=r.x && y>=r.y &&
           (int64_t)x<(int64_t)r.x+r.width && (int64_t)y<(int64_t)r.y+r.height;
}
static int descendant(InteractionImpl *i,PocketUiHandle h,PocketUiHandle root) {
    if(!pocket_ui_handle_valid(root))return 1;
    for(unsigned n=0;n<MAX_DEPTH && pocket_ui_handle_valid(h);n++) {
        if(equal(h,root))return 1;
        PocketUiSnapshot s;
        if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return 0;
        h=s.parent;
    }
    return 0;
}
static PocketInteractionStatus root_now(InteractionImpl *i,PocketUiHandle *root) {
    *root=i->scene_root;
    if(!i->overlays)return POCKET_INTERACTION_OK;
    PocketOverlaySnapshot o;
    PocketOverlayStatus status=pocket_overlay_input_capture(i->overlays,&o);
    if(status==POCKET_OVERLAY_NOT_FOUND)return POCKET_INTERACTION_OK;
    if(status!=POCKET_OVERLAY_OK)return POCKET_INTERACTION_STALE_HANDLE;
    PocketComponentSnapshot c;
    if(pocket_component_snapshot(i->components,o.spec.root,&c)!=POCKET_COMPONENT_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    *root=c.root;
    return POCKET_INTERACTION_OK;
}
static PocketInteractionStatus eligible(InteractionImpl *i,PocketUiHandle h,int focus) {
    PocketUiSnapshot s;
    if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return POCKET_INTERACTION_STALE_HANDLE;
    if(focus && !s.properties.focusable)return POCKET_INTERACTION_FOCUS_REJECTED;
    PocketUiHandle root={0};
    PocketInteractionStatus status=root_now(i,&root);
    if(status!=POCKET_INTERACTION_OK)return status;
    if(!descendant(i,h,root) || (focus && !descendant(i,h,i->focus_scope)))
        return POCKET_INTERACTION_FOCUS_REJECTED;
    for(unsigned n=0;n<MAX_DEPTH && pocket_ui_handle_valid(h);n++) {
        if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return POCKET_INTERACTION_STALE_HANDLE;
        if(!mounted(s.phase) || !s.properties.visible || !s.properties.enabled)
            return POCKET_INTERACTION_FOCUS_REJECTED;
        h=s.parent;
    }
    return pocket_ui_handle_valid(h)?POCKET_INTERACTION_FOCUS_REJECTED:POCKET_INTERACTION_OK;
}
static void semantic(InteractionImpl *i,PocketUiHandle h,uint32_t bit,int on) {
    PocketUiSnapshot s;
    if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return;
    uint64_t previous=s.properties.semantic_state;
    if(on)s.properties.semantic_state|=bit;
    else s.properties.semantic_state&=~(uint64_t)bit;
    if(previous==s.properties.semantic_state)return;
    (void)pocket_ui_update(i->tree,h,POCKET_UI_PROP_SEMANTIC_STATE,&s.properties);
}
static PocketInteractionStatus emit(InteractionImpl *i,PocketUiHandle h,uint32_t type,
                                     const PointerSlot *p,int32_t dx,int32_t dy,int *prevented) {
    PocketUiEvent e={0};
    e.type=type;
    e.timestamp_ms=i->clock_ms;
    if(p) {
        e.x=p->x;e.y=p->y;e.pointer_id=p->id;
        e.delta_x=dx;e.delta_y=dy;e.velocity_x=p->velocity_x;e.velocity_y=p->velocity_y;
        e.data=((uint64_t)p->id<<32)|(i->clock_ms&0xffffffffULL);
    }
    i->dispatching++;
    PocketUiStatus s=pocket_ui_dispatch_event(i->tree,h,&e);
    i->dispatching--;
    if(prevented)*prevented=e.default_prevented||e.cancelled;
    return s==POCKET_UI_OK?POCKET_INTERACTION_OK:POCKET_INTERACTION_STALE_HANDLE;
}
static PocketInteractionStatus focus_change(InteractionImpl *i,PocketUiHandle target) {
    if(i->focus_changing)return POCKET_INTERACTION_BUSY;
    if(equal(i->focused,target))return POCKET_INTERACTION_OK;
    i->focus_changing=1;
    PocketUiHandle old=i->focused;
    i->focused=(PocketUiHandle){0};
    if(pocket_ui_handle_valid(old)) {
        semantic(i,old,POCKET_STATE_FOCUSED,0);
        (void)emit(i,old,POCKET_UI_EVENT_FOCUS_LOST,NULL,0,0,NULL);
    }
    PocketInteractionStatus result=POCKET_INTERACTION_OK;
    if(pocket_ui_handle_valid(target)) {
        result=eligible(i,target,1);
        if(result==POCKET_INTERACTION_OK) {
            i->focused=target;
            semantic(i,target,POCKET_STATE_FOCUSED,1);
            (void)emit(i,target,POCKET_UI_EVENT_FOCUS_GAINED,NULL,0,0,NULL);
            result=eligible(i,target,1);
            if(result!=POCKET_INTERACTION_OK) {
                i->focused=(PocketUiHandle){0};
                semantic(i,target,POCKET_STATE_FOCUSED,0);
                (void)emit(i,target,POCKET_UI_EVENT_FOCUS_LOST,NULL,0,0,NULL);
            }
        }
    }
    i->focus_changing=0;
    return result;
}
static PointerSlot *slot_for(InteractionImpl *i,uint32_t id,int create) {
    PointerSlot *empty=NULL;
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++) {
        PointerSlot *p=&i->pointers[n];
        if(p->active && p->id==id)return p;
        if(!p->active && !empty)empty=p;
    }
    if(create && empty) { memset(empty,0,sizeof(*empty));empty->active=1;empty->id=id;return empty; }
    return NULL;
}
static void cancel_slot(InteractionImpl *i,PointerSlot *p,int keep_until_up) {
    if(!p->active)return;
    PointerSlot old=*p;
    /* Invalidate before callbacks: reentrant callbacks cannot cancel twice. */
    memset(p,0,sizeof(*p));
    if(keep_until_up) { p->id=old.id;p->active=1;p->suppressed=1; }
    if(old.suppressed)return;
    semantic(i,old.down_target,POCKET_STATE_PRESSED,0);
    PocketUiHandle target=pocket_ui_handle_valid(old.capture)?old.capture:old.down_target;
    (void)emit(i,target,POCKET_UI_EVENT_POINTER_CANCEL,&old,0,0,NULL);
    if(old.motion_event)(void)emit(i,old.motion_target,POCKET_UI_EVENT_GESTURE_CANCEL,&old,0,0,NULL);
}
static void cancel_everything(InteractionImpl *i,int keep_until_up) {
    memset(i->pending,0,sizeof(i->pending));
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++)cancel_slot(i,&i->pointers[n],keep_until_up);
    i->cancel_requested=0;
}
static FocusRecord *history(InteractionImpl *i,PocketUiHandle root,int create) {
    FocusRecord *empty=NULL;
    for(unsigned n=0;n<FOCUS_HISTORY;n++) {
        FocusRecord *r=&i->history[n];
        if(equal(r->root,root))return r;
        if(pocket_ui_handle_valid(r->root) && !live(i,r->root))memset(r,0,sizeof(*r));
        if(!pocket_ui_handle_valid(r->root) && !empty)empty=r;
    }
    if(create && empty) { empty->root=root;return empty; }
    return NULL;
}
static PocketInteractionStatus sync_context(InteractionImpl *i) {
    PocketUiHandle root={0};
    PocketInteractionStatus s=root_now(i,&root);
    if(s!=POCKET_INTERACTION_OK) { cancel_everything(i,1);return s; }
    uint64_t epoch=pocket_overlay_input_epoch(i->overlays);
    if(epoch!=i->overlay_epoch) {
        i->overlay_epoch=epoch;
        cancel_everything(i,1);
    }
    if(!equal(root,i->current_root)) {
        FocusRecord *old=history(i,i->current_root,1);
        if(!old)return POCKET_INTERACTION_BUDGET_EXHAUSTED;
        old->focused=i->focused;old->scope=i->focus_scope;
        cancel_everything(i,1);
        (void)focus_change(i,(PocketUiHandle){0});
        i->current_root=root;
        i->focus_scope=(PocketUiHandle){0};
        FocusRecord *saved=history(i,root,0);
        if(saved) {
            i->focus_scope=live(i,saved->scope)?saved->scope:(PocketUiHandle){0};
            if(eligible(i,saved->focused,1)==POCKET_INTERACTION_OK)(void)focus_change(i,saved->focused);
        }
    }
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++) {
        PointerSlot *p=&i->pointers[n];
        if(!p->active || p->suppressed)continue;
        PocketUiHandle target=pocket_ui_handle_valid(p->capture)?p->capture:p->down_target;
        if(eligible(i,target,0)!=POCKET_INTERACTION_OK || !equal(p->root,root))cancel_slot(i,p,1);
    }
    if(i->cancel_requested)cancel_everything(i,1);
    return POCKET_INTERACTION_OK;
}
static PocketInteractionStatus hit_node(InteractionImpl *i,PocketUiHandle h,int32_t x,int32_t y,
                                         PocketUiHandle *out,unsigned depth,unsigned *budget) {
    if(depth>=MAX_DEPTH || !*budget)return POCKET_INTERACTION_BUDGET_EXHAUSTED;
    --*budget;
    PocketUiSnapshot s;
    if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return POCKET_INTERACTION_STALE_HANDLE;
    if(!mounted(s.phase) || !s.properties.visible || !s.properties.enabled)return POCKET_INTERACTION_NO_TARGET;
    PocketLayoutResult l;
    int has=pocket_layout_result(i->layout,h,&l)==POCKET_UI_OK;
    if(has && l.clip_valid && !within(l.clip,x,y))return POCKET_INTERACTION_NO_TARGET;
    PocketUiHandle child={0};
    if(pocket_ui_first_child(i->tree,h,&child)==POCKET_UI_OK) {
        while(pocket_ui_handle_valid(child)) {
            PocketInteractionStatus result=hit_node(i,child,x,y,out,depth+1,budget);
            if(result==POCKET_INTERACTION_OK)return result;
            if(result!=POCKET_INTERACTION_NO_TARGET && result!=POCKET_INTERACTION_STALE_HANDLE)return result;
            PocketUiHandle next={0};
            if(pocket_ui_next_sibling(i->tree,child,&next)!=POCKET_UI_OK)break;
            child=next;
        }
    }
    if(within(has?l.geometry:s.properties.geometry,x,y) &&
       (s.properties.clickable || s.properties.focusable || s.type==POCKET_UI_SCROLL || s.type==POCKET_UI_INPUT)) {
        *out=h;return POCKET_INTERACTION_OK;
    }
    return POCKET_INTERACTION_NO_TARGET;
}
static uint32_t mask_for(InteractionImpl *i,PocketUiHandle h) {
    for(unsigned n=0;n<POCKET_GESTURE_MAX_BINDINGS;n++)if(equal(i->bindings[n].target,h))return i->bindings[n].mask;
    PocketUiSnapshot s;
    if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)return 0;
    return s.type==POCKET_UI_SCROLL ? POCKET_GESTURE_SCROLL_Y|POCKET_GESTURE_FLICK :
           s.properties.clickable ? POCKET_GESTURE_TAP : 0;
}
static int64_t abs64(int64_t n) { return n<0?-n:n; }
static int32_t clamp32(int64_t n) { return n>INT32_MAX?INT32_MAX:n<INT32_MIN?INT32_MIN:(int32_t)n; }
static uint64_t deadline(uint64_t a,uint32_t delta) { return UINT64_MAX-a<delta?UINT64_MAX:a+delta; }
static void tap_candidate(InteractionImpl *i,PointerSlot *p) {
    PocketUiHandle h=p->down_target;
    for(unsigned n=0;n<MAX_DEPTH && pocket_ui_handle_valid(h);n++) {
        uint32_t mask=mask_for(i,h);
        if(mask&(POCKET_GESTURE_TAP|POCKET_GESTURE_DOUBLE_TAP|POCKET_GESTURE_LONG_PRESS)) {
            p->tap_target=h;p->tap_mask=mask;return;
        }
        if(equal(h,p->root))break;
        PocketUiSnapshot s;if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)break;
        h=s.parent;
    }
}
static void motion_candidate(InteractionImpl *i,PointerSlot *p) {
    PocketUiHandle h=pocket_ui_handle_valid(p->capture)?p->capture:p->down_target;
    int64_t dx=abs64((int64_t)p->x-p->start_x),dy=abs64((int64_t)p->y-p->start_y);
    for(unsigned n=0;n<MAX_DEPTH && pocket_ui_handle_valid(h);n++) {
        uint32_t mask=mask_for(i,h),event=0;
        if(mask&POCKET_GESTURE_DRAG)event=POCKET_UI_EVENT_DRAG_BEGIN;
        else if(mask&POCKET_GESTURE_PAN)event=POCKET_UI_EVENT_PAN_BEGIN;
        else if(((mask&POCKET_GESTURE_SCROLL_X) && dx>=dy) ||
                ((mask&POCKET_GESTURE_SCROLL_Y) && dy>=dx))event=POCKET_UI_EVENT_SCROLL_BEGIN;
        if(event && eligible(i,h,0)==POCKET_INTERACTION_OK) {
            p->motion_target=h;p->motion_event=event;
            if(!equal(h,p->down_target))(void)emit(i,p->down_target,POCKET_UI_EVENT_POINTER_CANCEL,p,0,0,NULL);
            semantic(i,p->down_target,POCKET_STATE_PRESSED,0);
            p->capture=h;return;
        }
        if(equal(h,p->root))break;
        PocketUiSnapshot s;if(pocket_ui_snapshot(i->tree,h,&s)!=POCKET_UI_OK)break;
        h=s.parent;
    }
}
static PocketInteractionStatus advance(InteractionImpl *i,uint64_t now) {
    if(now<i->clock_ms)return POCKET_INTERACTION_CLOCK_REVERSED;
    i->clock_ms=now;
    PocketInteractionStatus s=sync_context(i);
    if(s!=POCKET_INTERACTION_OK)return s;
    for(unsigned n=0;n<PENDING_TAPS;n++) {
        PendingTap *t=&i->pending[n];
        if(!t->active || now<t->due)continue;
        PointerSlot p=t->pointer;t->active=0;
        if(equal(p.root,i->current_root) && eligible(i,p.tap_target,0)==POCKET_INTERACTION_OK &&
           (p.tap_mask&POCKET_GESTURE_TAP))
            (void)emit(i,p.tap_target,POCKET_UI_EVENT_TAP,&p,0,0,NULL);
        if(sync_context(i)!=POCKET_INTERACTION_OK)return POCKET_INTERACTION_STALE_HANDLE;
    }
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++) {
        PointerSlot *p=&i->pointers[n];
        if(!p->active || p->suppressed || p->moved || p->long_sent || p->prevented ||
           !(p->tap_mask&POCKET_GESTURE_LONG_PRESS) || now<deadline(p->down_ms,POCKET_GESTURE_LONG_MS))continue;
        if(eligible(i,p->tap_target,0)!=POCKET_INTERACTION_OK) { cancel_slot(i,p,1);continue; }
        p->long_sent=1;
        (void)emit(i,p->tap_target,POCKET_UI_EVENT_LONG_PRESS,p,0,0,NULL);
        if(sync_context(i)!=POCKET_INTERACTION_OK)return POCKET_INTERACTION_STALE_HANDLE;
    }
    return POCKET_INTERACTION_OK;
}
static PocketInteractionStatus tap_release(InteractionImpl *i,const PointerSlot *p) {
    if(!(p->tap_mask&POCKET_GESTURE_DOUBLE_TAP))
        return (p->tap_mask&POCKET_GESTURE_TAP)?emit(i,p->tap_target,POCKET_UI_EVENT_TAP,p,0,0,NULL):POCKET_INTERACTION_OK;
    PendingTap *empty=NULL;
    for(unsigned n=0;n<PENDING_TAPS;n++) {
        PendingTap *t=&i->pending[n];
        if(!t->active && !empty)empty=t;
        if(t->active && equal(t->pointer.tap_target,p->tap_target) && equal(t->pointer.root,p->root) &&
           i->clock_ms<t->due && abs64((int64_t)t->pointer.x-p->x)<=2*POCKET_GESTURE_SLOP &&
           abs64((int64_t)t->pointer.y-p->y)<=2*POCKET_GESTURE_SLOP) {
            t->active=0;return emit(i,p->tap_target,POCKET_UI_EVENT_DOUBLE_TAP,p,0,0,NULL);
        }
    }
    if(!empty)return POCKET_INTERACTION_BUDGET_EXHAUSTED;
    empty->active=1;empty->pointer=*p;empty->due=deadline(i->clock_ms,POCKET_GESTURE_DOUBLE_MS);
    return POCKET_INTERACTION_OK;
}
static PocketInteractionStatus finish(InteractionImpl *i,PocketInteractionStatus result) {
    if(i->cancel_requested)cancel_everything(i,1);
    i->processing=0;
    return result;
}
PocketInteractionStatus pocket_interaction_init(PocketInteractionRuntime *r,const PocketInteractionConfig *c) {
    if(!r || r->impl || !c || !c->tree || !c->layout || !c->components || !pocket_ui_handle_valid(c->scene_root))
        return POCKET_INTERACTION_INVALID_ARGUMENT;
    PocketUiSnapshot s;
    if(pocket_ui_snapshot(c->tree,c->scene_root,&s)!=POCKET_UI_OK)return POCKET_INTERACTION_STALE_HANDLE;
    InteractionImpl *i=calloc(1,sizeof(*i));
    if(!i)return POCKET_INTERACTION_BUDGET_EXHAUSTED;
    i->tree=c->tree;i->layout=c->layout;i->components=c->components;i->overlays=c->overlays;
    i->scene_root=c->scene_root;i->current_root=c->scene_root;
    i->overlay_epoch=pocket_overlay_input_epoch(c->overlays);r->impl=i;
    return POCKET_INTERACTION_OK;
}
void pocket_interaction_dispose(PocketInteractionRuntime *r) {
    InteractionImpl *i=ii(r);if(!i)return;
    /* Disposal belongs to the outer owner, never to an event callback. */
    if(i->processing || i->dispatching) { i->cancel_requested=1;return; }
    cancel_everything(i,0);(void)focus_change(i,(PocketUiHandle){0});free(i);r->impl=NULL;
}
PocketInteractionStatus pocket_interaction_set_scene_root(PocketInteractionRuntime *r,PocketUiHandle root) {
    InteractionImpl *i=ii(r);
    if(!i || !pocket_ui_handle_valid(root))return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->dispatching)return POCKET_INTERACTION_BUSY;
    if(!live(i,root))return POCKET_INTERACTION_STALE_HANDLE;
    i->scene_root=root;
    return sync_context(i);
}
PocketInteractionStatus pocket_interaction_hit_test(PocketInteractionRuntime *r,int32_t x,int32_t y,PocketUiHandle *out) {
    InteractionImpl *i=ii(r);if(!i || !out)return POCKET_INTERACTION_INVALID_ARGUMENT;
    *out=(PocketUiHandle){0};PocketUiHandle root={0};
    PocketInteractionStatus s=root_now(i,&root);if(s!=POCKET_INTERACTION_OK)return s;
    unsigned budget=HIT_BUDGET;return hit_node(i,root,x,y,out,0,&budget);
}
PocketInteractionStatus pocket_interaction_set_gestures(PocketInteractionRuntime *r,PocketUiHandle h,uint32_t mask) {
    InteractionImpl *i=ii(r);
    if(!i || !pocket_ui_handle_valid(h) || (mask&~POCKET_GESTURE_ALL))return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(!live(i,h))return POCKET_INTERACTION_STALE_HANDLE;
    if(i->processing || i->dispatching)return POCKET_INTERACTION_BUSY;
    GestureBinding *empty=NULL;
    for(unsigned n=0;n<POCKET_GESTURE_MAX_BINDINGS;n++) {
        GestureBinding *b=&i->bindings[n];
        if(equal(b->target,h)) { b->mask=mask;return POCKET_INTERACTION_OK; }
        if(pocket_ui_handle_valid(b->target) && !live(i,b->target))memset(b,0,sizeof(*b));
        if(!pocket_ui_handle_valid(b->target) && !empty)empty=b;
    }
    if(!empty)return POCKET_INTERACTION_BUDGET_EXHAUSTED;
    *empty=(GestureBinding){h,mask};return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_capture(PocketInteractionRuntime *r,uint32_t id,PocketUiHandle h) {
    InteractionImpl *i=ii(r);PointerSlot *p=i?slot_for(i,id,0):NULL;
    if(!p || p->suppressed)return POCKET_INTERACTION_POINTER_NOT_ACTIVE;
    PocketUiHandle root={0};
    if(root_now(i,&root)!=POCKET_INTERACTION_OK || !equal(root,p->root) ||
       i->overlay_epoch!=pocket_overlay_input_epoch(i->overlays) ||
       !pocket_ui_handle_valid(h) || eligible(i,h,0)!=POCKET_INTERACTION_OK)return POCKET_INTERACTION_CAPTURE_ERROR;
    p->capture=h;
    if(!equal(h,p->down_target))p->prevented=1;
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_release_capture(PocketInteractionRuntime *r,uint32_t id) {
    InteractionImpl *i=ii(r);PointerSlot *p=i?slot_for(i,id,0):NULL;
    if(!p || p->suppressed)return POCKET_INTERACTION_POINTER_NOT_ACTIVE;
    p->capture=(PocketUiHandle){0};return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_pointer(PocketInteractionRuntime *r,const PocketPointerEvent *e) {
    InteractionImpl *i=ii(r);
    if(!i || !e || e->phase<POCKET_POINTER_DOWN || e->phase>POCKET_POINTER_CANCEL)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->processing || i->dispatching)return POCKET_INTERACTION_BUSY;
    if(e->timestamp_ms<i->last_input_ms)return POCKET_INTERACTION_CLOCK_REVERSED;
    i->processing=1;i->last_input_ms=e->timestamp_ms;
    /* Input edges win ties with a recognizer deadline. In particular a cancel
     * or a movement crossing slop must never manufacture a long press. */
    PointerSlot *existing=slot_for(i,e->pointer_id,0);
    if(e->phase==POCKET_POINTER_CANCEL) {
        if(existing)existing->prevented=1;
        for(unsigned n=0;n<PENDING_TAPS;n++)
            if(i->pending[n].active && i->pending[n].pointer.id==e->pointer_id)i->pending[n].active=0;
    }
    if(existing && (e->phase==POCKET_POINTER_MOVE || e->phase==POCKET_POINTER_UP) &&
       (abs64((int64_t)e->x-existing->start_x)>POCKET_GESTURE_SLOP ||
        abs64((int64_t)e->y-existing->start_y)>POCKET_GESTURE_SLOP))existing->moved=1;
    PocketInteractionStatus status=advance(i,e->timestamp_ms>i->clock_ms?e->timestamp_ms:i->clock_ms);
    if(status!=POCKET_INTERACTION_OK)return finish(i,status);
    PointerSlot *p=slot_for(i,e->pointer_id,0);
    if(e->phase==POCKET_POINTER_DOWN) {
        if(p)return finish(i,POCKET_INTERACTION_POINTER_BUSY);
        PocketUiHandle target={0};
        status=pocket_interaction_hit_test(r,e->x,e->y,&target);
        if(status!=POCKET_INTERACTION_OK)return finish(i,status);
        p=slot_for(i,e->pointer_id,1);
        if(!p)return finish(i,POCKET_INTERACTION_POINTER_BUSY);
        p->down_target=target;p->root=i->current_root;p->x=p->start_x=e->x;p->y=p->start_y=e->y;
        p->down_ms=e->timestamp_ms;p->sample_ms=e->timestamp_ms;
        tap_candidate(i,p);semantic(i,target,POCKET_STATE_PRESSED,1);
        if(eligible(i,target,1)==POCKET_INTERACTION_OK)(void)focus_change(i,target);
        status=emit(i,target,POCKET_UI_EVENT_POINTER_DOWN,p,0,0,&p->prevented);
        if(status!=POCKET_INTERACTION_OK)cancel_slot(i,p,1);
        (void)sync_context(i);
        return finish(i,status);
    }
    if(!p)return finish(i,POCKET_INTERACTION_POINTER_NOT_ACTIVE);
    if(p->suppressed) {
        if(e->phase==POCKET_POINTER_UP || e->phase==POCKET_POINTER_CANCEL)memset(p,0,sizeof(*p));
        return finish(i,POCKET_INTERACTION_OK);
    }
    if(e->phase==POCKET_POINTER_CANCEL) { cancel_slot(i,p,0);return finish(i,POCKET_INTERACTION_OK); }
    int32_t dx=clamp32((int64_t)e->x-p->x),dy=clamp32((int64_t)e->y-p->y);
    if((dx || dy) && e->timestamp_ms>p->sample_ms) {
        uint64_t dt=e->timestamp_ms-p->sample_ms;
        p->velocity_x=clamp32((int64_t)dx*1000/(int64_t)(dt>INT32_MAX?INT32_MAX:dt));
        p->velocity_y=clamp32((int64_t)dy*1000/(int64_t)(dt>INT32_MAX?INT32_MAX:dt));
        p->velocity_ms=e->timestamp_ms;
    }
    p->sample_ms=e->timestamp_ms;p->x=e->x;p->y=e->y;
    if(abs64((int64_t)p->x-p->start_x)>POCKET_GESTURE_SLOP || abs64((int64_t)p->y-p->start_y)>POCKET_GESTURE_SLOP)p->moved=1;
    PocketUiHandle raw=pocket_ui_handle_valid(p->capture)?p->capture:p->down_target;
    int prevented=0;
    status=emit(i,raw,e->phase==POCKET_POINTER_MOVE?POCKET_UI_EVENT_POINTER_MOVE:POCKET_UI_EVENT_POINTER_UP,p,dx,dy,&prevented);
    p->prevented|=prevented;
    if(status!=POCKET_INTERACTION_OK) { cancel_slot(i,p,0);return finish(i,status); }
    (void)sync_context(i);
    if(p->suppressed) {
        if(e->phase==POCKET_POINTER_UP)memset(p,0,sizeof(*p));
        return finish(i,POCKET_INTERACTION_OK);
    }
    if(e->phase==POCKET_POINTER_MOVE && p->moved && !p->prevented && !p->long_sent) {
        uint32_t previous=p->motion_event;
        if(!previous)motion_candidate(i,p);
        if(p->motion_event && eligible(i,p->motion_target,0)==POCKET_INTERACTION_OK)
            (void)emit(i,p->motion_target,p->motion_event+(previous?1:0),p,dx,dy,NULL);
        (void)sync_context(i);
    }
    if(e->phase==POCKET_POINTER_UP) {
        PointerSlot done=*p;memset(p,0,sizeof(*p));semantic(i,done.down_target,POCKET_STATE_PRESSED,0);
        if(done.motion_event && eligible(i,done.motion_target,0)==POCKET_INTERACTION_OK) {
            (void)emit(i,done.motion_target,done.motion_event+2,&done,dx,dy,NULL);
            if((mask_for(i,done.motion_target)&POCKET_GESTURE_FLICK) &&
               e->timestamp_ms>=done.velocity_ms && e->timestamp_ms-done.velocity_ms<=150 &&
               (abs64(done.velocity_x)>=POCKET_GESTURE_FLICK_PPS || abs64(done.velocity_y)>=POCKET_GESTURE_FLICK_PPS) &&
               eligible(i,done.motion_target,0)==POCKET_INTERACTION_OK)
                (void)emit(i,done.motion_target,POCKET_UI_EVENT_FLICK,&done,0,0,NULL);
        } else if(!done.moved && !done.long_sent && !done.prevented && eligible(i,done.tap_target,0)==POCKET_INTERACTION_OK) {
            PocketUiHandle hit={0};
            if(pocket_interaction_hit_test(r,e->x,e->y,&hit)==POCKET_INTERACTION_OK && descendant(i,hit,done.tap_target))
                status=tap_release(i,&done);
        }
    }
    return finish(i,status);
}
void pocket_interaction_cancel_all(PocketInteractionRuntime *r,uint64_t now) {
    InteractionImpl *i=ii(r);if(!i)return;
    if(i->dispatching) { i->cancel_requested=1;return; }
    if(now>i->clock_ms)i->clock_ms=now;
    cancel_everything(i,0);
}
PocketInteractionStatus pocket_interaction_tick(PocketInteractionRuntime *r,uint64_t now) {
    InteractionImpl *i=ii(r);if(!i)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->processing || i->dispatching)return POCKET_INTERACTION_BUSY;
    i->processing=1;
    PocketInteractionStatus s=advance(i,now);
    if(s==POCKET_INTERACTION_OK && pocket_ui_handle_valid(i->focused) && eligible(i,i->focused,1)!=POCKET_INTERACTION_OK)
        (void)focus_change(i,(PocketUiHandle){0});
    return finish(i,s);
}
uint32_t pocket_interaction_next_deadline(const PocketInteractionRuntime *r,uint64_t now) {
    const InteractionImpl *i=cii(r);if(!i)return UINT32_MAX;
    uint64_t due=UINT64_MAX;
    for(unsigned n=0;n<PENDING_TAPS;n++)if(i->pending[n].active && i->pending[n].due<due)due=i->pending[n].due;
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++) {
        const PointerSlot *p=&i->pointers[n];
        if(p->active && !p->suppressed && !p->moved && !p->long_sent && !p->prevented && (p->tap_mask&POCKET_GESTURE_LONG_PRESS)) {
            uint64_t d=deadline(p->down_ms,POCKET_GESTURE_LONG_MS);if(d<due)due=d;
        }
    }
    return due==UINT64_MAX?UINT32_MAX:due<=now?0:due-now>UINT32_MAX?UINT32_MAX:(uint32_t)(due-now);
}
PocketInteractionStatus pocket_interaction_set_focus_scope(PocketInteractionRuntime *r,PocketUiHandle h) {
    InteractionImpl *i=ii(r);if(!i)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->focus_changing)return POCKET_INTERACTION_BUSY;
    if(pocket_ui_handle_valid(h) && !live(i,h))return POCKET_INTERACTION_STALE_HANDLE;
    i->focus_scope=h;
    if(pocket_ui_handle_valid(i->focused) && eligible(i,i->focused,1)!=POCKET_INTERACTION_OK)return focus_change(i,(PocketUiHandle){0});
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_focus(PocketInteractionRuntime *r,PocketUiHandle h) {
    InteractionImpl *i=ii(r);if(!i || !pocket_ui_handle_valid(h))return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->focus_changing)return POCKET_INTERACTION_BUSY;
    PocketInteractionStatus s=sync_context(i);if(s!=POCKET_INTERACTION_OK)return s;
    s=eligible(i,h,1);return s==POCKET_INTERACTION_OK?focus_change(i,h):s;
}
PocketInteractionStatus pocket_interaction_clear_focus(PocketInteractionRuntime *r) {
    InteractionImpl *i=ii(r);return i?focus_change(i,(PocketUiHandle){0}):POCKET_INTERACTION_INVALID_ARGUMENT;
}
PocketInteractionStatus pocket_interaction_key(PocketInteractionRuntime *r,PocketKeyAction action) {
    InteractionImpl *i=ii(r);
    if(!i || action<POCKET_KEY_ACTION_BACK || action>POCKET_KEY_ACTION_RIGHT)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(i->processing || i->dispatching)return POCKET_INTERACTION_BUSY;
    i->processing=1;
    PocketInteractionStatus s=sync_context(i);if(s!=POCKET_INTERACTION_OK)return finish(i,s);
    if(!pocket_ui_handle_valid(i->focused))return finish(i,POCKET_INTERACTION_NO_TARGET);
    s=eligible(i,i->focused,1);
    if(s!=POCKET_INTERACTION_OK) {
        (void)focus_change(i,(PocketUiHandle){0});
        return finish(i,s==POCKET_INTERACTION_FOCUS_REJECTED?POCKET_INTERACTION_NO_TARGET:s);
    }
    PocketUiEvent e={0};e.type=POCKET_UI_EVENT_KEY_ACTION;e.data=(uint64_t)action;e.timestamp_ms=i->clock_ms;
    i->dispatching++;PocketUiStatus us=pocket_ui_dispatch_event(i->tree,i->focused,&e);i->dispatching--;
    return finish(i,us==POCKET_UI_OK?POCKET_INTERACTION_OK:POCKET_INTERACTION_STALE_HANDLE);
}
PocketInteractionStatus pocket_interaction_snapshot(const PocketInteractionRuntime *r,PocketInteractionSnapshot *out) {
    const InteractionImpl *i=cii(r);if(!i || !out)return POCKET_INTERACTION_INVALID_ARGUMENT;
    out->focused=i->focused;out->focus_scope=i->focus_scope;out->active_pointers=0;
    for(unsigned n=0;n<POCKET_INTERACTION_MAX_POINTERS;n++)if(i->pointers[n].active && !i->pointers[n].suppressed)out->active_pointers++;
    return POCKET_INTERACTION_OK;
}
