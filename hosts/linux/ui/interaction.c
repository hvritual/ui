#include "interaction.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int active;
    uint32_t id;
    PocketUiHandle down_target;
    PocketUiHandle capture;
    int32_t x;
    int32_t y;
} PointerSlot;

typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketComponentRuntime *components;
    PocketOverlayManager *overlays;
    PocketUiHandle scene_root;
    PocketUiHandle focus_scope;
    PocketUiHandle focused;
    PointerSlot pointers[POCKET_INTERACTION_MAX_POINTERS];
} InteractionImpl;

static InteractionImpl *ii(PocketInteractionRuntime *runtime) {
    return runtime ? (InteractionImpl *)runtime->impl : NULL;
}
static const InteractionImpl *cii(const PocketInteractionRuntime *runtime) {
    return runtime ? (const InteractionImpl *)runtime->impl : NULL;
}
static int handle_equal(PocketUiHandle a,PocketUiHandle b) {
    return a.slot==b.slot&&a.generation==b.generation;
}
static int point_in(PocketUiRect rect,int32_t x,int32_t y) {
    if(rect.width<=0||rect.height<=0)return 0;
    return x>=rect.x&&y>=rect.y&&
           (int64_t)x<(int64_t)rect.x+rect.width&&
           (int64_t)y<(int64_t)rect.y+rect.height;
}
static int mounted_phase(PocketUiLifecyclePhase phase) {
    return phase==POCKET_UI_PHASE_MOUNTED||phase==POCKET_UI_PHASE_UPDATED||
           phase==POCKET_UI_PHASE_LAYOUT||phase==POCKET_UI_PHASE_PAINT;
}
static int descendant_of(InteractionImpl *impl,PocketUiHandle node,PocketUiHandle scope) {
    if(!pocket_ui_handle_valid(scope))return 1;
    PocketUiHandle current=node;
    for(uint32_t depth=0;depth<256U&&pocket_ui_handle_valid(current);depth++) {
        if(handle_equal(current,scope))return 1;
        PocketUiSnapshot snapshot;
        if(pocket_ui_snapshot(impl->tree,current,&snapshot)!=POCKET_UI_OK)return 0;
        current=snapshot.parent;
    }
    return 0;
}
static PocketInteractionStatus resolve_overlay_root(InteractionImpl *impl,PocketUiHandle *root) {
    *root=impl->scene_root;
    if(!impl->overlays)return POCKET_INTERACTION_OK;
    PocketOverlaySnapshot overlay;
    PocketOverlayStatus status=pocket_overlay_input_capture(impl->overlays,&overlay);
    if(status==POCKET_OVERLAY_NOT_FOUND)return POCKET_INTERACTION_OK;
    if(status!=POCKET_OVERLAY_OK)return POCKET_INTERACTION_STALE_HANDLE;
    PocketComponentSnapshot component;
    if(pocket_component_snapshot(impl->components,overlay.spec.root,&component)!=POCKET_COMPONENT_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    *root=component.root;
    return POCKET_INTERACTION_OK;
}
static PocketInteractionStatus hit_node(InteractionImpl *impl,PocketUiHandle node,
                                        int32_t x,int32_t y,PocketUiHandle *out,
                                        uint32_t depth) {
    if(depth>256U)return POCKET_INTERACTION_STALE_HANDLE;
    PocketUiSnapshot snapshot;
    if(pocket_ui_snapshot(impl->tree,node,&snapshot)!=POCKET_UI_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    if(!mounted_phase(snapshot.phase)||!snapshot.properties.visible||!snapshot.properties.enabled)
        return POCKET_INTERACTION_NO_TARGET;

    PocketLayoutResult result;
    int has_layout=pocket_layout_result(impl->layout,node,&result)==POCKET_UI_OK;
    if(has_layout&&result.clip_valid&&!point_in(result.clip,x,y))
        return POCKET_INTERACTION_NO_TARGET;

    PocketUiHandle child={0};
    if(pocket_ui_first_child(impl->tree,node,&child)==POCKET_UI_OK) {
        while(pocket_ui_handle_valid(child)) {
            PocketUiHandle found={0};
            PocketInteractionStatus status=hit_node(impl,child,x,y,&found,depth+1U);
            if(status==POCKET_INTERACTION_OK) {
                *out=found;
                return POCKET_INTERACTION_OK;
            }
            if(status!=POCKET_INTERACTION_NO_TARGET&&status!=POCKET_INTERACTION_STALE_HANDLE)
                return status;
            PocketUiHandle next={0};
            if(pocket_ui_next_sibling(impl->tree,child,&next)!=POCKET_UI_OK)break;
            child=next;
        }
    }

    PocketUiRect geometry=has_layout?result.geometry:snapshot.properties.geometry;
    if(!point_in(geometry,x,y))return POCKET_INTERACTION_NO_TARGET;
    if(snapshot.properties.clickable||snapshot.properties.focusable||
       snapshot.type==POCKET_UI_SCROLL||snapshot.type==POCKET_UI_INPUT) {
        *out=node;
        return POCKET_INTERACTION_OK;
    }
    return POCKET_INTERACTION_NO_TARGET;
}
static PointerSlot *pointer_slot(InteractionImpl *impl,uint32_t id,int create) {
    PointerSlot *free_slot=NULL;
    for(uint32_t i=0;i<POCKET_INTERACTION_MAX_POINTERS;i++) {
        PointerSlot *slot=&impl->pointers[i];
        if(slot->active&&slot->id==id)return slot;
        if(!slot->active&&!free_slot)free_slot=slot;
    }
    if(create&&free_slot) {
        memset(free_slot,0,sizeof(*free_slot));
        free_slot->active=1;
        free_slot->id=id;
        return free_slot;
    }
    return NULL;
}
static uint32_t active_pointer_count(const InteractionImpl *impl) {
    uint32_t count=0;
    for(uint32_t i=0;i<POCKET_INTERACTION_MAX_POINTERS;i++)if(impl->pointers[i].active)count++;
    return count;
}
static PocketInteractionStatus dispatch_pointer(InteractionImpl *impl,PocketUiHandle target,
                                                const PocketPointerEvent *pointer) {
    if(!pocket_ui_handle_valid(target))return POCKET_INTERACTION_NO_TARGET;
    PocketUiEvent event;
    memset(&event,0,sizeof(event));
    event.type=pointer->phase==POCKET_POINTER_DOWN?POCKET_UI_EVENT_POINTER_DOWN:
               pointer->phase==POCKET_POINTER_MOVE?POCKET_UI_EVENT_POINTER_MOVE:
               pointer->phase==POCKET_POINTER_UP?POCKET_UI_EVENT_POINTER_UP:
               POCKET_UI_EVENT_POINTER_CANCEL;
    event.x=pointer->x;
    event.y=pointer->y;
    event.data=((uint64_t)pointer->pointer_id<<32)|(pointer->timestamp_ms&0xffffffffULL);
    PocketUiStatus status=pocket_ui_dispatch_event(impl->tree,target,&event);
    return status==POCKET_UI_OK?POCKET_INTERACTION_OK:POCKET_INTERACTION_STALE_HANDLE;
}
static PocketInteractionStatus update_focus_state(InteractionImpl *impl,PocketUiHandle target,int focused) {
    PocketUiSnapshot snapshot;
    if(pocket_ui_snapshot(impl->tree,target,&snapshot)!=POCKET_UI_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    PocketUiProperties properties=snapshot.properties;
    if(focused)properties.semantic_state|=POCKET_STATE_FOCUSED;
    else properties.semantic_state&=~(uint64_t)POCKET_STATE_FOCUSED;
    return pocket_ui_update(impl->tree,target,POCKET_UI_PROP_SEMANTIC_STATE,&properties)==POCKET_UI_OK?
           POCKET_INTERACTION_OK:POCKET_INTERACTION_STALE_HANDLE;
}
PocketInteractionStatus pocket_interaction_init(PocketInteractionRuntime *runtime,
                                                 const PocketInteractionConfig *config) {
    if(!runtime||runtime->impl||!config||!config->tree||!config->layout||
       !config->components||!pocket_ui_handle_valid(config->scene_root))
        return POCKET_INTERACTION_INVALID_ARGUMENT;
    PocketUiSnapshot snapshot;
    if(pocket_ui_snapshot(config->tree,config->scene_root,&snapshot)!=POCKET_UI_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    InteractionImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_INTERACTION_INVALID_ARGUMENT;
    impl->tree=config->tree;impl->layout=config->layout;impl->components=config->components;
    impl->overlays=config->overlays;impl->scene_root=config->scene_root;
    runtime->impl=impl;
    return POCKET_INTERACTION_OK;
}
void pocket_interaction_dispose(PocketInteractionRuntime *runtime) {
    InteractionImpl *impl=ii(runtime);
    if(!impl)return;
    free(impl);
    runtime->impl=NULL;
}
PocketInteractionStatus pocket_interaction_set_scene_root(PocketInteractionRuntime *runtime,
                                                           PocketUiHandle root) {
    InteractionImpl *impl=ii(runtime);PocketUiSnapshot snapshot;
    if(!impl||!pocket_ui_handle_valid(root))return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(pocket_ui_snapshot(impl->tree,root,&snapshot)!=POCKET_UI_OK)return POCKET_INTERACTION_STALE_HANDLE;
    impl->scene_root=root;
    if(pocket_ui_handle_valid(impl->focused)&&!descendant_of(impl,impl->focused,root))
        (void)pocket_interaction_clear_focus(runtime);
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_hit_test(PocketInteractionRuntime *runtime,
                                                     int32_t x,int32_t y,PocketUiHandle *out) {
    InteractionImpl *impl=ii(runtime);
    if(!impl||!out)return POCKET_INTERACTION_INVALID_ARGUMENT;
    PocketUiHandle root={0};
    PocketInteractionStatus status=resolve_overlay_root(impl,&root);
    if(status!=POCKET_INTERACTION_OK)return status;
    return hit_node(impl,root,x,y,out,0);
}
PocketInteractionStatus pocket_interaction_capture(PocketInteractionRuntime *runtime,
                                                    uint32_t pointer_id,
                                                    PocketUiHandle target) {
    InteractionImpl *impl=ii(runtime);PointerSlot *slot=impl?pointer_slot(impl,pointer_id,0):NULL;
    if(!slot)return POCKET_INTERACTION_POINTER_NOT_ACTIVE;
    PocketUiSnapshot snapshot;
    if(!pocket_ui_handle_valid(target)||pocket_ui_snapshot(impl->tree,target,&snapshot)!=POCKET_UI_OK)
        return POCKET_INTERACTION_CAPTURE_ERROR;
    slot->capture=target;
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_release_capture(PocketInteractionRuntime *runtime,
                                                            uint32_t pointer_id) {
    InteractionImpl *impl=ii(runtime);PointerSlot *slot=impl?pointer_slot(impl,pointer_id,0):NULL;
    if(!slot)return POCKET_INTERACTION_POINTER_NOT_ACTIVE;
    slot->capture=(PocketUiHandle){0};
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_pointer(PocketInteractionRuntime *runtime,
                                                    const PocketPointerEvent *event) {
    InteractionImpl *impl=ii(runtime);
    if(!impl||!event||event->phase<POCKET_POINTER_DOWN||
       event->phase>POCKET_POINTER_CANCEL)
        return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(event->phase==POCKET_POINTER_DOWN) {
        if(pointer_slot(impl,event->pointer_id,0))return POCKET_INTERACTION_POINTER_BUSY;
        PointerSlot *slot=pointer_slot(impl,event->pointer_id,1);
        if(!slot)return POCKET_INTERACTION_POINTER_BUSY;
        PocketUiHandle target={0};
        PocketInteractionStatus hit=pocket_interaction_hit_test(runtime,event->x,event->y,&target);
        if(hit!=POCKET_INTERACTION_OK) {
            slot->active=0;
            return hit;
        }
        slot->down_target=target;slot->x=event->x;slot->y=event->y;
        PocketUiSnapshot snapshot;
        if(pocket_ui_snapshot(impl->tree,target,&snapshot)==POCKET_UI_OK&&snapshot.properties.focusable)
            (void)pocket_interaction_focus(runtime,target);
        return dispatch_pointer(impl,target,event);
    }
    PointerSlot *slot=pointer_slot(impl,event->pointer_id,0);
    if(!slot)return POCKET_INTERACTION_POINTER_NOT_ACTIVE;
    slot->x=event->x;slot->y=event->y;
    PocketUiHandle target=slot->capture;
    if(!pocket_ui_handle_valid(target)) {
        PocketInteractionStatus hit=pocket_interaction_hit_test(runtime,event->x,event->y,&target);
        if(hit!=POCKET_INTERACTION_OK)target=slot->down_target;
    }
    PocketInteractionStatus status=dispatch_pointer(impl,target,event);
    if(status==POCKET_INTERACTION_STALE_HANDLE||
       event->phase==POCKET_POINTER_UP||event->phase==POCKET_POINTER_CANCEL)
        memset(slot,0,sizeof(*slot));
    return status;
}
void pocket_interaction_cancel_all(PocketInteractionRuntime *runtime,uint64_t timestamp_ms) {
    InteractionImpl *impl=ii(runtime);
    if(!impl)return;
    for(uint32_t i=0;i<POCKET_INTERACTION_MAX_POINTERS;i++) {
        PointerSlot *slot=&impl->pointers[i];
        if(!slot->active)continue;
        PocketPointerEvent event={slot->id,POCKET_POINTER_CANCEL,slot->x,slot->y,timestamp_ms};
        PocketUiHandle target=pocket_ui_handle_valid(slot->capture)?slot->capture:slot->down_target;
        (void)dispatch_pointer(impl,target,&event);
        memset(slot,0,sizeof(*slot));
    }
}
PocketInteractionStatus pocket_interaction_set_focus_scope(PocketInteractionRuntime *runtime,
                                                            PocketUiHandle scope) {
    InteractionImpl *impl=ii(runtime);
    if(!impl)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(pocket_ui_handle_valid(scope)) {
        PocketUiSnapshot snapshot;
        if(pocket_ui_snapshot(impl->tree,scope,&snapshot)!=POCKET_UI_OK)
            return POCKET_INTERACTION_STALE_HANDLE;
    }
    impl->focus_scope=scope;
    if(pocket_ui_handle_valid(impl->focused)&&!descendant_of(impl,impl->focused,scope))
        return pocket_interaction_clear_focus(runtime);
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_focus(PocketInteractionRuntime *runtime,
                                                  PocketUiHandle target) {
    InteractionImpl *impl=ii(runtime);PocketUiSnapshot snapshot;
    if(!impl||!pocket_ui_handle_valid(target))return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(pocket_ui_snapshot(impl->tree,target,&snapshot)!=POCKET_UI_OK)
        return POCKET_INTERACTION_STALE_HANDLE;
    if(!snapshot.properties.visible||!snapshot.properties.enabled||!snapshot.properties.focusable||
       !mounted_phase(snapshot.phase)||!descendant_of(impl,target,impl->focus_scope))
        return POCKET_INTERACTION_FOCUS_REJECTED;
    if(handle_equal(impl->focused,target))return POCKET_INTERACTION_OK;
    if(pocket_ui_handle_valid(impl->focused)) {
        PocketUiHandle old=impl->focused;
        (void)update_focus_state(impl,old,0);
        PocketUiEvent lost={0};
        lost.type=POCKET_UI_EVENT_FOCUS_LOST;
        (void)pocket_ui_dispatch_event(impl->tree,old,&lost);
    }
    impl->focused=target;
    PocketInteractionStatus status=update_focus_state(impl,target,1);
    if(status!=POCKET_INTERACTION_OK) {
        impl->focused=(PocketUiHandle){0};
        return status;
    }
    PocketUiEvent gained={0};
    gained.type=POCKET_UI_EVENT_FOCUS_GAINED;
    (void)pocket_ui_dispatch_event(impl->tree,target,&gained);
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_clear_focus(PocketInteractionRuntime *runtime) {
    InteractionImpl *impl=ii(runtime);
    if(!impl)return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(!pocket_ui_handle_valid(impl->focused))return POCKET_INTERACTION_OK;
    PocketUiHandle old=impl->focused;
    impl->focused=(PocketUiHandle){0};
    (void)update_focus_state(impl,old,0);
    PocketUiEvent lost={0};
    lost.type=POCKET_UI_EVENT_FOCUS_LOST;
    (void)pocket_ui_dispatch_event(impl->tree,old,&lost);
    return POCKET_INTERACTION_OK;
}
PocketInteractionStatus pocket_interaction_key(PocketInteractionRuntime *runtime,
                                                PocketKeyAction action) {
    InteractionImpl *impl=ii(runtime);
    if(!impl||action<POCKET_KEY_ACTION_BACK||action>POCKET_KEY_ACTION_RIGHT)
        return POCKET_INTERACTION_INVALID_ARGUMENT;
    if(!pocket_ui_handle_valid(impl->focused))return POCKET_INTERACTION_NO_TARGET;
    PocketUiSnapshot snapshot;
    if(pocket_ui_snapshot(impl->tree,impl->focused,&snapshot)!=POCKET_UI_OK) {
        impl->focused=(PocketUiHandle){0};
        return POCKET_INTERACTION_STALE_HANDLE;
    }
    PocketUiEvent event={0};
    event.type=POCKET_UI_EVENT_KEY_ACTION;
    event.data=(uint64_t)action;
    return pocket_ui_dispatch_event(impl->tree,impl->focused,&event)==POCKET_UI_OK?
           POCKET_INTERACTION_OK:POCKET_INTERACTION_STALE_HANDLE;
}
PocketInteractionStatus pocket_interaction_snapshot(const PocketInteractionRuntime *runtime,
                                                     PocketInteractionSnapshot *out) {
    const InteractionImpl *impl=cii(runtime);
    if(!impl||!out)return POCKET_INTERACTION_INVALID_ARGUMENT;
    out->focused=impl->focused;
    out->focus_scope=impl->focus_scope;
    out->active_pointers=active_pointer_count(impl);
    return POCKET_INTERACTION_OK;
}
