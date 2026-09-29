#include "model.h"

#include <stdlib.h>
#include <string.h>

#define MODEL_MAX_ITEMS 65535U
#define REPEATER_DEFAULT_MAX 64U
#define VIRTUAL_DEFAULT_POOL 32U
#define VIRTUAL_MAX_POOL 512U

typedef struct {
    PocketComponentHandle component;
    uint64_t key;
    uint32_t index;
    int bound;
} ModelSlot;

typedef struct {
    PocketComponentRuntime *components;
    PocketComponentHandle parent;
    PocketComponentKind item_kind;
    PocketModelApi model;
    ModelSlot *slots;
    uint32_t capacity;
    uint32_t count;
    uint64_t bind_calls;
    uint64_t revision;
} RepeaterImpl;

typedef struct {
    PocketComponentRuntime *components;
    PocketComponentHandle parent;
    PocketComponentKind item_kind;
    PocketModelApi model;
    ModelSlot *pool;
    uint32_t max_pool;
    uint32_t pool_size;
    uint32_t peak_pool_size;
    uint32_t overscan;
    uint32_t visible_first;
    uint32_t visible_count;
    uint32_t materialized_first;
    uint32_t materialized_count;
    uint64_t bind_calls;
    uint64_t recycle_count;
    uint64_t revision;
    uint64_t selected_key;
    uint64_t focused_key;
} VirtualImpl;

static int model_valid(const PocketModelApi *model) {
    return model&&model->count&&model->key_at&&model->bind;
}
static PocketModelStatus parent_valid(PocketComponentRuntime *components,
                                      PocketComponentHandle parent,int virtual_only) {
    PocketComponentSnapshot snapshot;
    if(!components||pocket_component_snapshot(components,parent,&snapshot)!=POCKET_COMPONENT_OK)
        return POCKET_MODEL_STALE_COMPONENT;
    if(virtual_only) {
        if(snapshot.kind!=POCKET_COMPONENT_LIST&&snapshot.kind!=POCKET_COMPONENT_GRID&&
           snapshot.kind!=POCKET_COMPONENT_SCROLL)
            return POCKET_MODEL_INVALID_ARGUMENT;
    }
    return POCKET_MODEL_OK;
}
static PocketModelStatus key_at(const PocketModelApi *model,uint32_t index,uint64_t *key) {
    if(!model->key_at(model->context,index,key)||!*key)return POCKET_MODEL_INVALID_ARGUMENT;
    return POCKET_MODEL_OK;
}
static PocketModelStatus validate_window_keys(const PocketModelApi *model,uint32_t first,uint32_t count) {
    for(uint32_t i=0;i<count;i++) {
        uint64_t a=0;PocketModelStatus status=key_at(model,first+i,&a);
        if(status!=POCKET_MODEL_OK)return status;
        for(uint32_t j=0;j<i;j++) {
            uint64_t b=0;status=key_at(model,first+j,&b);
            if(status!=POCKET_MODEL_OK)return status;
            if(a==b)return POCKET_MODEL_DUPLICATE_KEY;
        }
    }
    return POCKET_MODEL_OK;
}
static int model_contains_key(const PocketModelApi *model,uint64_t key) {
    if(!key)return 1;
    uint32_t count=model->count(model->context);
    if(count>MODEL_MAX_ITEMS)return 0;
    for(uint32_t i=0;i<count;i++) {
        uint64_t current=0;
        if(!model->key_at(model->context,i,&current)||!current)return 0;
        if(current==key)return 1;
    }
    return 0;
}
PocketModelStatus pocket_repeater_init(PocketRepeater *repeater,const PocketRepeaterConfig *config) {
    if(!repeater||repeater->impl||!config||!model_valid(&config->model)||
       !pocket_component_handle_valid(config->parent))
        return POCKET_MODEL_INVALID_ARGUMENT;
    PocketModelStatus parent=parent_valid(config->components,config->parent,0);
    if(parent!=POCKET_MODEL_OK)return parent;
    uint32_t capacity=config->max_items?config->max_items:REPEATER_DEFAULT_MAX;
    if(!capacity||capacity>MODEL_MAX_ITEMS)return POCKET_MODEL_INVALID_ARGUMENT;
    RepeaterImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_MODEL_RESOURCE_EXHAUSTED;
    impl->slots=calloc(capacity,sizeof(*impl->slots));
    if(!impl->slots){free(impl);return POCKET_MODEL_RESOURCE_EXHAUSTED;}
    impl->components=config->components;impl->parent=config->parent;impl->item_kind=config->item_kind;
    impl->model=config->model;impl->capacity=capacity;repeater->impl=impl;
    return POCKET_MODEL_OK;
}
void pocket_repeater_dispose(PocketRepeater *repeater) {
    RepeaterImpl *impl=repeater?(RepeaterImpl *)repeater->impl:NULL;
    if(!impl)return;
    for(uint32_t i=0;i<impl->count;i++)
        if(pocket_component_handle_valid(impl->slots[i].component))
            (void)pocket_component_destroy(impl->components,impl->slots[i].component);
    free(impl->slots);free(impl);repeater->impl=NULL;
}
PocketModelStatus pocket_repeater_sync(PocketRepeater *repeater,uint64_t revision) {
    RepeaterImpl *impl=repeater?(RepeaterImpl *)repeater->impl:NULL;
    if(!impl)return POCKET_MODEL_INVALID_ARGUMENT;
    uint32_t count=impl->model.count(impl->model.context);
    if(count>impl->capacity)return POCKET_MODEL_RESOURCE_EXHAUSTED;
    PocketModelStatus keys=validate_window_keys(&impl->model,0,count);
    if(keys!=POCKET_MODEL_OK)return keys;
    while(impl->count>count) {
        impl->count--;
        if(pocket_component_destroy(impl->components,impl->slots[impl->count].component)!=POCKET_COMPONENT_OK)
            return POCKET_MODEL_STALE_COMPONENT;
        memset(&impl->slots[impl->count],0,sizeof(impl->slots[impl->count]));
    }
    for(uint32_t i=0;i<count;i++) {
        uint64_t key=0;PocketModelStatus status=key_at(&impl->model,i,&key);
        if(status!=POCKET_MODEL_OK)return status;
        ModelSlot *slot=&impl->slots[i];
        if(i>=impl->count) {
            if(pocket_component_create(impl->components,impl->item_kind,impl->parent,NULL,&slot->component)
               !=POCKET_COMPONENT_OK)
                return POCKET_MODEL_RESOURCE_EXHAUSTED;
            impl->count++;
        }
        if(!slot->bound||slot->key!=key||slot->index!=i||impl->revision!=revision) {
            if(impl->model.bind(impl->model.context,i,key,impl->components,slot->component)!=POCKET_COMPONENT_OK)
                return POCKET_MODEL_DELEGATE_ERROR;
            slot->key=key;slot->index=i;slot->bound=1;impl->bind_calls++;
        }
    }
    impl->revision=revision;
    return POCKET_MODEL_OK;
}
size_t pocket_repeater_count(const PocketRepeater *repeater) {
    const RepeaterImpl *impl=repeater?(const RepeaterImpl *)repeater->impl:NULL;
    return impl?impl->count:0;
}
uint64_t pocket_repeater_bind_calls(const PocketRepeater *repeater) {
    const RepeaterImpl *impl=repeater?(const RepeaterImpl *)repeater->impl:NULL;
    return impl?impl->bind_calls:0;
}
PocketModelStatus pocket_virtual_collection_init(PocketVirtualCollection *collection,
                                                  const PocketVirtualCollectionConfig *config) {
    if(!collection||collection->impl||!config||!model_valid(&config->model)||
       !pocket_component_handle_valid(config->parent))
        return POCKET_MODEL_INVALID_ARGUMENT;
    PocketModelStatus parent=parent_valid(config->components,config->parent,1);
    if(parent!=POCKET_MODEL_OK)return parent;
    uint32_t max_pool=config->max_pool?config->max_pool:VIRTUAL_DEFAULT_POOL;
    if(!max_pool||max_pool>VIRTUAL_MAX_POOL)return POCKET_MODEL_INVALID_ARGUMENT;
    VirtualImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_MODEL_RESOURCE_EXHAUSTED;
    impl->pool=calloc(max_pool,sizeof(*impl->pool));
    if(!impl->pool){free(impl);return POCKET_MODEL_RESOURCE_EXHAUSTED;}
    impl->components=config->components;impl->parent=config->parent;impl->item_kind=config->item_kind;
    impl->model=config->model;impl->overscan=config->overscan;impl->max_pool=max_pool;
    collection->impl=impl;
    return POCKET_MODEL_OK;
}
void pocket_virtual_collection_dispose(PocketVirtualCollection *collection) {
    VirtualImpl *impl=collection?(VirtualImpl *)collection->impl:NULL;
    if(!impl)return;
    for(uint32_t i=0;i<impl->pool_size;i++)
        if(pocket_component_handle_valid(impl->pool[i].component))
            (void)pocket_component_destroy(impl->components,impl->pool[i].component);
    free(impl->pool);free(impl);collection->impl=NULL;
}
static PocketModelStatus ensure_pool(VirtualImpl *impl,uint32_t desired) {
    if(desired>impl->max_pool)return POCKET_MODEL_RESOURCE_EXHAUSTED;
    while(impl->pool_size<desired) {
        ModelSlot *slot=&impl->pool[impl->pool_size];
        if(pocket_component_create(impl->components,impl->item_kind,impl->parent,NULL,&slot->component)
           !=POCKET_COMPONENT_OK)
            return POCKET_MODEL_RESOURCE_EXHAUSTED;
        impl->pool_size++;
        if(impl->pool_size>impl->peak_pool_size)impl->peak_pool_size=impl->pool_size;
    }
    return POCKET_MODEL_OK;
}
static PocketModelStatus sync_slot_state(VirtualImpl *impl,ModelSlot *slot) {
    PocketComponentSnapshot snapshot;
    if(pocket_component_snapshot(impl->components,slot->component,&snapshot)!=POCKET_COMPONENT_OK)
        return POCKET_MODEL_STALE_COMPONENT;
    uint32_t states=snapshot.props.states&~(POCKET_STATE_SELECTED|POCKET_STATE_FOCUSED);
    if(slot->bound&&slot->key==impl->selected_key)states|=POCKET_STATE_SELECTED;
    if(slot->bound&&slot->key==impl->focused_key)states|=POCKET_STATE_FOCUSED;
    return pocket_component_set_states(impl->components,slot->component,states)==POCKET_COMPONENT_OK ?
           POCKET_MODEL_OK:POCKET_MODEL_STALE_COMPONENT;
}
static PocketModelStatus sync_pool_states(VirtualImpl *impl) {
    for(uint32_t i=0;i<impl->pool_size;i++) {
        if(!impl->pool[i].bound)continue;
        PocketModelStatus status=sync_slot_state(impl,&impl->pool[i]);
        if(status!=POCKET_MODEL_OK)return status;
    }
    return POCKET_MODEL_OK;
}
static PocketModelStatus bind_window(VirtualImpl *impl,uint32_t start,uint32_t count,int force) {
    PocketModelStatus keys=validate_window_keys(&impl->model,start,count);
    if(keys!=POCKET_MODEL_OK)return keys;
    PocketModelStatus pool=ensure_pool(impl,count);
    if(pool!=POCKET_MODEL_OK)return pool;
    for(uint32_t i=0;i<impl->pool_size;i++) {
        ModelSlot *slot=&impl->pool[i];
        if(i>=count) {
            if(pocket_component_set_visible(impl->components,slot->component,0)!=POCKET_COMPONENT_OK)
                return POCKET_MODEL_STALE_COMPONENT;
            continue;
        }
        uint32_t index=start+i;uint64_t key=0;PocketModelStatus status=key_at(&impl->model,index,&key);
        if(status!=POCKET_MODEL_OK)return status;
        if(pocket_component_set_visible(impl->components,slot->component,1)!=POCKET_COMPONENT_OK)
            return POCKET_MODEL_STALE_COMPONENT;
        if(force||!slot->bound||slot->key!=key||slot->index!=index) {
            if(slot->bound&&slot->key!=key)impl->recycle_count++;
            if(impl->model.bind(impl->model.context,index,key,impl->components,slot->component)
               !=POCKET_COMPONENT_OK)
                return POCKET_MODEL_DELEGATE_ERROR;
            slot->key=key;slot->index=index;slot->bound=1;impl->bind_calls++;
        }
        PocketModelStatus state_status=sync_slot_state(impl,slot);
        if(state_status!=POCKET_MODEL_OK)return state_status;
    }
    impl->materialized_first=start;impl->materialized_count=count;
    return POCKET_MODEL_OK;
}
PocketModelStatus pocket_virtual_collection_set_window(PocketVirtualCollection *collection,
                                                        uint32_t first,uint32_t visible_count) {
    VirtualImpl *impl=collection?(VirtualImpl *)collection->impl:NULL;
    if(!impl||!visible_count)return POCKET_MODEL_INVALID_ARGUMENT;
    uint32_t total=impl->model.count(impl->model.context);
    if(total>MODEL_MAX_ITEMS||first>total)return POCKET_MODEL_INVALID_ARGUMENT;
    if(first==total&&total)return POCKET_MODEL_INVALID_ARGUMENT;
    uint32_t end=visible_count>total-first?total:first+visible_count;
    uint32_t start=first>impl->overscan?first-impl->overscan:0U;
    uint32_t materialized_end=end+impl->overscan;
    if(materialized_end<end||materialized_end>total)materialized_end=total;
    uint32_t count=materialized_end-start;
    PocketModelStatus status=bind_window(impl,start,count,0);
    if(status!=POCKET_MODEL_OK)return status;
    impl->visible_first=first;impl->visible_count=end-first;
    if(impl->selected_key&&!model_contains_key(&impl->model,impl->selected_key))impl->selected_key=0;
    if(impl->focused_key&&!model_contains_key(&impl->model,impl->focused_key))impl->focused_key=0;
    return POCKET_MODEL_OK;
}
PocketModelStatus pocket_virtual_collection_refresh(PocketVirtualCollection *collection,
                                                     uint64_t revision) {
    VirtualImpl *impl=collection?(VirtualImpl *)collection->impl:NULL;
    if(!impl)return POCKET_MODEL_INVALID_ARGUMENT;
    uint32_t total=impl->model.count(impl->model.context);
    if(total>MODEL_MAX_ITEMS)return POCKET_MODEL_INVALID_ARGUMENT;
    if(impl->materialized_first>total)return POCKET_MODEL_INVALID_ARGUMENT;
    uint32_t count=impl->materialized_count;
    if(count>total-impl->materialized_first)count=total-impl->materialized_first;
    int force=revision!=impl->revision;
    PocketModelStatus status=bind_window(impl,impl->materialized_first,count,force);
    if(status!=POCKET_MODEL_OK)return status;
    impl->revision=revision;
    if(impl->selected_key&&!model_contains_key(&impl->model,impl->selected_key))impl->selected_key=0;
    if(impl->focused_key&&!model_contains_key(&impl->model,impl->focused_key))impl->focused_key=0;
    return POCKET_MODEL_OK;
}
PocketModelStatus pocket_virtual_collection_select(PocketVirtualCollection *collection,uint64_t key) {
    VirtualImpl *impl=collection?(VirtualImpl *)collection->impl:NULL;
    if(!impl)return POCKET_MODEL_INVALID_ARGUMENT;
    if(key&&!model_contains_key(&impl->model,key))return POCKET_MODEL_KEY_NOT_FOUND;
    impl->selected_key=key;
    return sync_pool_states(impl);
}
PocketModelStatus pocket_virtual_collection_focus(PocketVirtualCollection *collection,uint64_t key) {
    VirtualImpl *impl=collection?(VirtualImpl *)collection->impl:NULL;
    if(!impl)return POCKET_MODEL_INVALID_ARGUMENT;
    if(key&&!model_contains_key(&impl->model,key))return POCKET_MODEL_KEY_NOT_FOUND;
    impl->focused_key=key;
    return sync_pool_states(impl);
}
PocketModelStatus pocket_virtual_collection_component_for_key(
    const PocketVirtualCollection *collection,uint64_t key,PocketComponentHandle *out) {
    const VirtualImpl *impl=collection?(const VirtualImpl *)collection->impl:NULL;
    if(!impl||!key||!out)return POCKET_MODEL_INVALID_ARGUMENT;
    for(uint32_t i=0;i<impl->pool_size;i++)
        if(impl->pool[i].bound&&impl->pool[i].key==key) {
            *out=impl->pool[i].component;
            return POCKET_MODEL_OK;
        }
    return POCKET_MODEL_KEY_NOT_FOUND;
}
PocketModelStatus pocket_virtual_collection_stats(const PocketVirtualCollection *collection,
                                                   PocketVirtualCollectionStats *out) {
    const VirtualImpl *impl=collection?(const VirtualImpl *)collection->impl:NULL;
    if(!impl||!out)return POCKET_MODEL_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    out->model_count=impl->model.count(impl->model.context);
    out->visible_first=impl->visible_first;out->visible_count=impl->visible_count;
    out->materialized_first=impl->materialized_first;out->materialized_count=impl->materialized_count;
    out->pool_size=impl->pool_size;out->peak_pool_size=impl->peak_pool_size;
    out->bind_calls=impl->bind_calls;out->recycle_count=impl->recycle_count;
    out->pool_bytes=(size_t)impl->max_pool*sizeof(ModelSlot);
    out->selected_key=impl->selected_key;out->focused_key=impl->focused_key;out->revision=impl->revision;
    return POCKET_MODEL_OK;
}
