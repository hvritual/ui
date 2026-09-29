#include "overlay.h"

#include <stdlib.h>
#include <string.h>

#define OVERLAY_DEFAULT_CAPACITY 32U
#define OVERLAY_MAX_CAPACITY 128U

typedef struct {
    int live;
    PocketOverlaySpec spec;
    uint64_t sequence;
    uint64_t z_order;
    uint64_t previous_focus;
} OverlayRecord;

typedef struct {
    PocketComponentRuntime *components;
    OverlayRecord *records;
    uint32_t capacity;
    uint64_t next_sequence;
    size_t live_count;
    uint64_t current_focus;
} OverlayImpl;

static OverlayImpl *oi(PocketOverlayManager *manager){return manager?(OverlayImpl *)manager->impl:NULL;}
static const OverlayImpl *coi(const PocketOverlayManager *manager){return manager?(const OverlayImpl *)manager->impl:NULL;}
static int kind_valid(PocketOverlayKind kind){return kind>=POCKET_OVERLAY_POPUP&&kind<=POCKET_OVERLAY_IME_CANDIDATE;}
static uint32_t layer_base(PocketOverlayKind kind){
    switch(kind){
        case POCKET_OVERLAY_POPUP:return 1000U;
        case POCKET_OVERLAY_TOAST:return 2000U;
        case POCKET_OVERLAY_MODAL:
        case POCKET_OVERLAY_DIALOG:
        case POCKET_OVERLAY_LOADING:return 3000U;
        case POCKET_OVERLAY_KEYBOARD:return 4000U;
        case POCKET_OVERLAY_IME_CANDIDATE:return 5000U;
        default:return 0;
    }
}
static OverlayRecord *by_id(OverlayImpl *impl,uint64_t id){
    if(!impl||!id)return NULL;
    for(uint32_t i=0;i<impl->capacity;i++)if(impl->records[i].live&&impl->records[i].spec.id==id)return &impl->records[i];
    return NULL;
}
static const OverlayRecord *by_id_const(const OverlayImpl *impl,uint64_t id){
    if(!impl||!id)return NULL;
    for(uint32_t i=0;i<impl->capacity;i++)if(impl->records[i].live&&impl->records[i].spec.id==id)return &impl->records[i];
    return NULL;
}
static OverlayRecord *top_record(OverlayImpl *impl,int actionable_only){
    OverlayRecord *best=NULL;
    for(uint32_t i=0;i<impl->capacity;i++){
        OverlayRecord *r=&impl->records[i];
        if(!r->live)continue;
        if(actionable_only && !r->spec.dismiss_on_back && !r->spec.captures_input && !r->spec.captures_focus)continue;
        if(!best||r->z_order>best->z_order||(r->z_order==best->z_order&&r->sequence>best->sequence))best=r;
    }
    return best;
}
static const OverlayRecord *top_record_const(const OverlayImpl *impl){
    const OverlayRecord *best=NULL;
    for(uint32_t i=0;i<impl->capacity;i++){
        const OverlayRecord *r=&impl->records[i];
        if(!r->live)continue;
        if(!best||r->z_order>best->z_order||(r->z_order==best->z_order&&r->sequence>best->sequence))best=r;
    }
    return best;
}
PocketOverlayStatus pocket_overlay_init(PocketOverlayManager *manager,const PocketOverlayConfig *config){
    if(!manager||manager->impl||!config||!config->components)return POCKET_OVERLAY_INVALID_ARGUMENT;
    uint32_t capacity=config->capacity?config->capacity:OVERLAY_DEFAULT_CAPACITY;
    if(!capacity||capacity>OVERLAY_MAX_CAPACITY)return POCKET_OVERLAY_INVALID_ARGUMENT;
    OverlayImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_OVERLAY_FULL;
    impl->records=calloc(capacity,sizeof(*impl->records));
    if(!impl->records){free(impl);return POCKET_OVERLAY_FULL;}
    impl->components=config->components;
    impl->capacity=capacity;
    impl->next_sequence=1;
    manager->impl=impl;
    return POCKET_OVERLAY_OK;
}
void pocket_overlay_dispose(PocketOverlayManager *manager){
    OverlayImpl *impl=oi(manager);
    if(!impl)return;
    for(uint32_t i=0;i<impl->capacity;i++)if(impl->records[i].live){
        if(impl->records[i].spec.owns_root)(void)pocket_component_destroy(impl->components,impl->records[i].spec.root);
        impl->records[i].live=0;
    }
    free(impl->records);free(impl);manager->impl=NULL;
}
PocketOverlayStatus pocket_overlay_present(PocketOverlayManager *manager,const PocketOverlaySpec *spec){
    OverlayImpl *impl=oi(manager);PocketComponentSnapshot snapshot;
    if(!impl||!spec||!spec->id||!kind_valid(spec->kind)||!pocket_component_handle_valid(spec->root)||
       spec->owns_root>1U||spec->captures_input>1U||spec->captures_focus>1U||spec->dismiss_on_back>1U||
       (spec->captures_focus&&!spec->focus_token))
        return POCKET_OVERLAY_INVALID_ARGUMENT;
    if(by_id(impl,spec->id))return POCKET_OVERLAY_DUPLICATE;
    if(pocket_component_snapshot(impl->components,spec->root,&snapshot)!=POCKET_COMPONENT_OK)
        return POCKET_OVERLAY_STALE_COMPONENT;
    if(spec->owns_root && pocket_component_handle_valid(snapshot.parent))
        return POCKET_OVERLAY_INVALID_ARGUMENT;
    if((spec->kind==POCKET_OVERLAY_MODAL || spec->kind==POCKET_OVERLAY_DIALOG ||
        spec->kind==POCKET_OVERLAY_LOADING) && !spec->captures_input)
        return POCKET_OVERLAY_INVALID_ARGUMENT;
    if((spec->kind==POCKET_OVERLAY_MODAL && snapshot.kind!=POCKET_COMPONENT_MODAL) ||
       (spec->kind==POCKET_OVERLAY_DIALOG && snapshot.kind!=POCKET_COMPONENT_DIALOG) ||
       (spec->kind==POCKET_OVERLAY_TOAST && snapshot.kind!=POCKET_COMPONENT_TOAST) ||
       (spec->kind==POCKET_OVERLAY_LOADING && snapshot.kind!=POCKET_COMPONENT_LOADING))
        return POCKET_OVERLAY_INVALID_ARGUMENT;
    OverlayRecord *record=NULL;
    for(uint32_t i=0;i<impl->capacity;i++)if(!impl->records[i].live){record=&impl->records[i];break;}
    if(!record)return POCKET_OVERLAY_FULL;
    if(impl->next_sequence==UINT64_MAX)return POCKET_OVERLAY_FULL;
    memset(record,0,sizeof(*record));
    record->live=1;
    record->spec=*spec;
    record->sequence=impl->next_sequence++;
    record->z_order=(uint64_t)layer_base(spec->kind)*1000000000ULL+record->sequence;
    if(spec->captures_focus){record->previous_focus=impl->current_focus;impl->current_focus=spec->focus_token;}
    impl->live_count++;
    return POCKET_OVERLAY_OK;
}
PocketOverlayStatus pocket_overlay_dismiss(PocketOverlayManager *manager,uint64_t id){
    OverlayImpl *impl=oi(manager);OverlayRecord *record=by_id(impl,id);
    if(!record)return POCKET_OVERLAY_NOT_FOUND;
    if(record->spec.captures_focus){
        if(impl->current_focus==record->spec.focus_token)
            impl->current_focus=record->previous_focus;
        for(uint32_t i=0;i<impl->capacity;i++){
            OverlayRecord *upper=&impl->records[i];
            if(upper->live&&upper!=record&&upper->spec.captures_focus&&
               upper->previous_focus==record->spec.focus_token)
                upper->previous_focus=record->previous_focus;
        }
    }
    PocketOverlaySpec spec=record->spec;
    record->live=0;
    if(impl->live_count)impl->live_count--;
    if(spec.owns_root&&(pocket_component_destroy(impl->components,spec.root)!=POCKET_COMPONENT_OK))
        return POCKET_OVERLAY_STALE_COMPONENT;
    return POCKET_OVERLAY_OK;
}
uint32_t pocket_overlay_dismiss_owner(PocketOverlayManager *manager,uint64_t owner_route){
    OverlayImpl *impl=oi(manager);
    if(!impl||!owner_route)return 0;
    uint32_t count=0;
    for(;;){
        OverlayRecord *best=NULL;
        for(uint32_t i=0;i<impl->capacity;i++){
            OverlayRecord *r=&impl->records[i];
            if(r->live&&r->spec.owner_route==owner_route&&(!best||r->z_order>best->z_order))best=r;
        }
        if(!best)break;
        uint64_t id=best->spec.id;(void)pocket_overlay_dismiss(manager,id);count++;
    }
    return count;
}
PocketOverlayStatus pocket_overlay_input_capture(const PocketOverlayManager *manager,
                                                 PocketOverlaySnapshot *out){
    const OverlayImpl *impl=coi(manager);
    if(!impl||!out)return POCKET_OVERLAY_INVALID_ARGUMENT;
    const OverlayRecord *best=NULL;
    for(uint32_t i=0;i<impl->capacity;i++){
        const OverlayRecord *r=&impl->records[i];
        if(!r->live||!r->spec.captures_input)continue;
        if(!best||r->z_order>best->z_order||
           (r->z_order==best->z_order&&r->sequence>best->sequence))best=r;
    }
    if(!best)return POCKET_OVERLAY_NOT_FOUND;
    out->spec=best->spec;
    out->z_order=best->z_order;
    out->previous_focus_token=best->previous_focus;
    return POCKET_OVERLAY_OK;
}
PocketOverlayStatus pocket_overlay_top(const PocketOverlayManager *manager,PocketOverlaySnapshot *out){
    const OverlayImpl *impl=coi(manager);
    if(!impl||!out)return POCKET_OVERLAY_INVALID_ARGUMENT;
    const OverlayRecord *r=top_record_const(impl);
    if(!r)return POCKET_OVERLAY_NOT_FOUND;
    out->spec=r->spec;
    out->z_order=r->z_order;
    out->previous_focus_token=r->previous_focus;
    return POCKET_OVERLAY_OK;
}
PocketOverlayStatus pocket_overlay_snapshot(const PocketOverlayManager *manager,uint64_t id,
                                            PocketOverlaySnapshot *out){
    const OverlayImpl *impl=coi(manager);if(!impl||!out)return POCKET_OVERLAY_INVALID_ARGUMENT;
    const OverlayRecord *r=by_id_const(impl,id);
    if(!r)return POCKET_OVERLAY_NOT_FOUND;
    out->spec=r->spec;
    out->z_order=r->z_order;
    out->previous_focus_token=r->previous_focus;
    return POCKET_OVERLAY_OK;
}
size_t pocket_overlay_count(const PocketOverlayManager *manager){
    const OverlayImpl *impl=coi(manager);
    return impl?impl->live_count:0;
}
int pocket_overlay_blocks_background(const PocketOverlayManager *manager){
    const OverlayImpl *impl=coi(manager);
    if(!impl)return 0;
    for(uint32_t i=0;i<impl->capacity;i++)if(impl->records[i].live&&impl->records[i].spec.captures_input)return 1;
    return 0;
}
uint64_t pocket_overlay_focus_token(const PocketOverlayManager *manager){
    const OverlayImpl *impl=coi(manager);
    return impl?impl->current_focus:0;
}
PocketOverlayStatus pocket_overlay_back(PocketOverlayManager *manager,int *consumed){
    OverlayImpl *impl=oi(manager);
    if(!impl||!consumed)return POCKET_OVERLAY_INVALID_ARGUMENT;
    *consumed=0;
    OverlayRecord *r=top_record(impl,1);
    if(!r)return POCKET_OVERLAY_OK;
    *consumed=1;
    if(r->spec.dismiss_on_back)return pocket_overlay_dismiss(manager,r->spec.id);
    return POCKET_OVERLAY_OK;
}
void pocket_overlay_navigation_cleanup(void *context,uint64_t route_id){
    if(context&&route_id)(void)pocket_overlay_dismiss_owner((PocketOverlayManager *)context,route_id);
}
