#include "reactive.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define REACTIVE_DEFAULT_NODES 256U
#define REACTIVE_DEFAULT_EFFECTS 128U
#define REACTIVE_DEFAULT_BINDINGS 256U
#define REACTIVE_DEFAULT_BUDGET 512U
#define REACTIVE_MAX_NODES 4096U
#define REACTIVE_MAX_EFFECTS 2048U
#define REACTIVE_MAX_BINDINGS 4096U
#define REACTIVE_MAX_EFFECT_RUNS_PER_FLUSH 16U

typedef enum {
    REACTIVE_NODE_SIGNAL = 1,
    REACTIVE_NODE_COMPUTED = 2
} ReactiveNodeKind;

typedef struct {
    uint32_t generation;
    int live;
    ReactiveNodeKind kind;
    PocketValueType type;
    PocketReactiveValue value;
    uint64_t version;
    PocketReactiveHandle deps[POCKET_REACTIVE_MAX_DEPS];
    uint64_t dep_versions[POCKET_REACTIVE_MAX_DEPS];
    uint32_t dep_count;
    PocketReactiveComputeFn compute;
    void *context;
    int dirty;
    int staged;
    PocketReactiveValue staged_value;
} ReactiveNode;

typedef struct {
    uint32_t generation;
    int live;
    PocketReactiveHandle deps[POCKET_REACTIVE_MAX_DEPS];
    uint64_t dep_versions[POCKET_REACTIVE_MAX_DEPS];
    uint32_t dep_count;
    PocketReactiveEffectFn effect;
    void *context;
    uint32_t runs_this_flush;
    int initial;
} ReactiveEffect;

typedef struct {
    uint32_t generation;
    int live;
    PocketReactiveHandle source;
    PocketComponentHandle component;
    PocketBindingTarget target;
    uint64_t source_version;
    int initial;
} ReactiveBinding;

typedef struct {
    ReactiveNode *nodes;
    ReactiveEffect *effects;
    ReactiveBinding *bindings;
    uint32_t node_capacity;
    uint32_t effect_capacity;
    uint32_t binding_capacity;
    uint32_t default_budget;
    uint32_t batch_depth;
    int transaction_active;
    uint64_t mutation_serial;
    PocketComponentRuntime *components;
} ReactiveImpl;

static ReactiveImpl *impl_of(PocketReactiveRuntime *runtime) {
    return runtime ? (ReactiveImpl *)runtime->impl : NULL;
}
static const ReactiveImpl *const_impl_of(const PocketReactiveRuntime *runtime) {
    return runtime ? (const ReactiveImpl *)runtime->impl : NULL;
}
PocketReactiveValue pocket_value_i64(int64_t value) {
    PocketReactiveValue v={POCKET_VALUE_I64,{0}};v.as.i64=value;return v;
}
PocketReactiveValue pocket_value_u64(uint64_t value) {
    PocketReactiveValue v={POCKET_VALUE_U64,{0}};v.as.u64=value;return v;
}
PocketReactiveValue pocket_value_bool(int value) {
    PocketReactiveValue v={POCKET_VALUE_BOOL,{0}};v.as.boolean=value?1:0;return v;
}
int pocket_reactive_handle_valid(PocketReactiveHandle handle) {
    return handle.slot!=0U && handle.generation!=0U;
}
static int type_valid(PocketValueType type) {
    return type>=POCKET_VALUE_I64 && type<=POCKET_VALUE_BOOL;
}
static int value_valid(PocketReactiveValue value) {
    return type_valid(value.type) &&
           (value.type!=POCKET_VALUE_BOOL || value.as.boolean==0 || value.as.boolean==1);
}
static int value_equal(PocketReactiveValue a,PocketReactiveValue b) {
    if(a.type!=b.type)return 0;
    if(a.type==POCKET_VALUE_I64)return a.as.i64==b.as.i64;
    if(a.type==POCKET_VALUE_U64)return a.as.u64==b.as.u64;
    return a.as.boolean==b.as.boolean;
}
static ReactiveNode *node_at(ReactiveImpl *impl,PocketReactiveHandle handle) {
    if(!impl||!pocket_reactive_handle_valid(handle)||handle.slot>impl->node_capacity)return NULL;
    ReactiveNode *node=&impl->nodes[handle.slot-1U];
    return node->live&&node->generation==handle.generation?node:NULL;
}
static const ReactiveNode *node_at_const(const ReactiveImpl *impl,PocketReactiveHandle handle) {
    if(!impl||!pocket_reactive_handle_valid(handle)||handle.slot>impl->node_capacity)return NULL;
    const ReactiveNode *node=&impl->nodes[handle.slot-1U];
    return node->live&&node->generation==handle.generation?node:NULL;
}
static ReactiveNode *allocate_node(ReactiveImpl *impl,PocketReactiveHandle *out) {
    for(uint32_t i=0;i<impl->node_capacity;i++) {
        ReactiveNode *node=&impl->nodes[i];
        if(!node->live) {
            uint32_t generation=node->generation+1U;
            if(!generation)generation=1U;
            memset(node,0,sizeof(*node));
            node->generation=generation;node->live=1;
            out->slot=i+1U;out->generation=generation;
            return node;
        }
    }
    return NULL;
}
static int deps_valid(const ReactiveImpl *impl,const PocketReactiveHandle *deps,uint32_t count) {
    if(count>POCKET_REACTIVE_MAX_DEPS || (count&&!deps))return 0;
    for(uint32_t i=0;i<count;i++) {
        if(!node_at_const(impl,deps[i]))return 0;
        for(uint32_t j=0;j<i;j++)
            if(deps[i].slot==deps[j].slot&&deps[i].generation==deps[j].generation)return 0;
    }
    return 1;
}
static int depends_on(const ReactiveImpl *impl,PocketReactiveHandle start,
                      PocketReactiveHandle target,uint32_t depth) {
    if(depth>impl->node_capacity)return 1;
    if(start.slot==target.slot&&start.generation==target.generation)return 1;
    const ReactiveNode *node=node_at_const(impl,start);
    if(!node||node->kind!=REACTIVE_NODE_COMPUTED)return 0;
    for(uint32_t i=0;i<node->dep_count;i++)
        if(depends_on(impl,node->deps[i],target,depth+1U))return 1;
    return 0;
}
static int dependency_changed(const ReactiveImpl *impl,const PocketReactiveHandle *deps,
                              const uint64_t *versions,uint32_t count) {
    for(uint32_t i=0;i<count;i++) {
        const ReactiveNode *dep=node_at_const(impl,deps[i]);
        if(!dep||dep->version!=versions[i])return 1;
    }
    return 0;
}
static int computed_ready(const ReactiveImpl *impl,const ReactiveNode *node) {
    for(uint32_t i=0;i<node->dep_count;i++) {
        const ReactiveNode *dep=node_at_const(impl,node->deps[i]);
        if(!dep)return 0;
        if(dep->kind==REACTIVE_NODE_COMPUTED &&
           (dep->dirty||dependency_changed(impl,dep->deps,dep->dep_versions,dep->dep_count)))
            return 0;
    }
    return 1;
}
static void snapshot_versions(const ReactiveImpl *impl,const PocketReactiveHandle *deps,
                              uint64_t *versions,uint32_t count) {
    for(uint32_t i=0;i<count;i++) {
        const ReactiveNode *dep=node_at_const(impl,deps[i]);
        versions[i]=dep?dep->version:0;
    }
}
static PocketReactiveStatus gather_values(const ReactiveImpl *impl,
                                          const PocketReactiveHandle *deps,uint32_t count,
                                          PocketReactiveValue *out) {
    for(uint32_t i=0;i<count;i++) {
        const ReactiveNode *dep=node_at_const(impl,deps[i]);
        if(!dep)return POCKET_REACTIVE_STALE_HANDLE;
        out[i]=dep->value;
    }
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_init(PocketReactiveRuntime *runtime,
                                           const PocketReactiveConfig *config) {
    if(!runtime||runtime->impl)return POCKET_REACTIVE_INVALID_ARGUMENT;
    uint32_t nc=config&&config->node_capacity?config->node_capacity:REACTIVE_DEFAULT_NODES;
    uint32_t ec=config&&config->effect_capacity?config->effect_capacity:REACTIVE_DEFAULT_EFFECTS;
    uint32_t bc=config&&config->binding_capacity?config->binding_capacity:REACTIVE_DEFAULT_BINDINGS;
    uint32_t budget=config&&config->default_flush_budget?config->default_flush_budget:REACTIVE_DEFAULT_BUDGET;
    if(!nc||nc>REACTIVE_MAX_NODES||ec>REACTIVE_MAX_EFFECTS||bc>REACTIVE_MAX_BINDINGS||!budget)
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    ReactiveImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    impl->nodes=calloc(nc,sizeof(*impl->nodes));
    impl->effects=ec?calloc(ec,sizeof(*impl->effects)):NULL;
    impl->bindings=bc?calloc(bc,sizeof(*impl->bindings)):NULL;
    if(!impl->nodes||(ec&&!impl->effects)||(bc&&!impl->bindings)) {
        free(impl->bindings);free(impl->effects);free(impl->nodes);free(impl);
        return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    }
    impl->node_capacity=nc;impl->effect_capacity=ec;impl->binding_capacity=bc;
    impl->default_budget=budget;impl->components=config?config->components:NULL;
    runtime->impl=impl;
    return POCKET_REACTIVE_OK;
}
void pocket_reactive_dispose(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl)return;
    free(impl->bindings);free(impl->effects);free(impl->nodes);free(impl);
    runtime->impl=NULL;
}
PocketReactiveStatus pocket_reactive_signal(PocketReactiveRuntime *runtime,
                                             PocketReactiveValue initial,
                                             PocketReactiveHandle *out) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!out||!value_valid(initial))return POCKET_REACTIVE_INVALID_ARGUMENT;
    PocketReactiveHandle handle={0};ReactiveNode *node=allocate_node(impl,&handle);
    if(!node)return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    node->kind=REACTIVE_NODE_SIGNAL;node->type=initial.type;node->value=initial;node->version=1;
    *out=handle;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_computed(PocketReactiveRuntime *runtime,
                                               PocketValueType output_type,
                                               const PocketReactiveHandle *dependencies,
                                               uint32_t dependency_count,
                                               PocketReactiveComputeFn compute,
                                               void *context,
                                               PocketReactiveHandle *out) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!out||!compute||!type_valid(output_type)||
       !deps_valid(impl,dependencies,dependency_count))
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    PocketReactiveHandle handle={0};ReactiveNode *node=allocate_node(impl,&handle);
    if(!node)return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    node->kind=REACTIVE_NODE_COMPUTED;node->type=output_type;node->compute=compute;
    node->context=context;node->dep_count=dependency_count;node->dirty=1;
    for(uint32_t i=0;i<dependency_count;i++)node->deps[i]=dependencies[i];
    *out=handle;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_rewire_computed(PocketReactiveRuntime *runtime,
                                                      PocketReactiveHandle computed,
                                                      const PocketReactiveHandle *dependencies,
                                                      uint32_t dependency_count) {
    ReactiveImpl *impl=impl_of(runtime);ReactiveNode *node=node_at(impl,computed);
    if(!node)return POCKET_REACTIVE_STALE_HANDLE;
    if(node->kind!=REACTIVE_NODE_COMPUTED||!deps_valid(impl,dependencies,dependency_count))
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    for(uint32_t i=0;i<dependency_count;i++)
        if(depends_on(impl,dependencies[i],computed,0))return POCKET_REACTIVE_CYCLE;
    memset(node->deps,0,sizeof(node->deps));memset(node->dep_versions,0,sizeof(node->dep_versions));
    node->dep_count=dependency_count;
    for(uint32_t i=0;i<dependency_count;i++)node->deps[i]=dependencies[i];
    node->dirty=1;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_get(const PocketReactiveRuntime *runtime,
                                          PocketReactiveHandle handle,
                                          PocketReactiveValue *out) {
    const ReactiveImpl *impl=const_impl_of(runtime);const ReactiveNode *node=node_at_const(impl,handle);
    if(!node)return POCKET_REACTIVE_STALE_HANDLE;
    if(!out)return POCKET_REACTIVE_INVALID_ARGUMENT;
    *out=node->value;
    return POCKET_REACTIVE_OK;
}
static void apply_signal(ReactiveImpl *impl,ReactiveNode *node,PocketReactiveValue value) {
    if(!value_equal(node->value,value)) {
        node->value=value;node->version++;
        if(!node->version)node->version=1;
        impl->mutation_serial++;
    }
}
PocketReactiveStatus pocket_reactive_set(PocketReactiveRuntime *runtime,
                                          PocketReactiveHandle signal,
                                          PocketReactiveValue value) {
    ReactiveImpl *impl=impl_of(runtime);ReactiveNode *node=node_at(impl,signal);
    if(!node)return POCKET_REACTIVE_STALE_HANDLE;
    if(node->kind!=REACTIVE_NODE_SIGNAL)return POCKET_REACTIVE_INVALID_ARGUMENT;
    if(!value_valid(value)||value.type!=node->type)return POCKET_REACTIVE_TYPE_MISMATCH;
    if(impl->transaction_active) {
        node->staged=1;node->staged_value=value;
        return POCKET_REACTIVE_OK;
    }
    apply_signal(impl,node,value);
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_destroy(PocketReactiveRuntime *runtime,
                                              PocketReactiveHandle handle) {
    ReactiveImpl *impl=impl_of(runtime);ReactiveNode *node=node_at(impl,handle);
    if(!node)return POCKET_REACTIVE_STALE_HANDLE;
    for(uint32_t i=0;i<impl->node_capacity;i++) {
        ReactiveNode *other=&impl->nodes[i];
        if(!other->live||other==node)continue;
        for(uint32_t d=0;d<other->dep_count;d++)
            if(other->deps[d].slot==handle.slot&&other->deps[d].generation==handle.generation)
                return POCKET_REACTIVE_BUSY;
    }
    for(uint32_t i=0;i<impl->effect_capacity;i++)if(impl->effects[i].live)
        for(uint32_t d=0;d<impl->effects[i].dep_count;d++)
            if(impl->effects[i].deps[d].slot==handle.slot&&
               impl->effects[i].deps[d].generation==handle.generation)
                return POCKET_REACTIVE_BUSY;
    for(uint32_t i=0;i<impl->binding_capacity;i++)
        if(impl->bindings[i].live&&impl->bindings[i].source.slot==handle.slot&&
           impl->bindings[i].source.generation==handle.generation)
            return POCKET_REACTIVE_BUSY;
    node->live=0;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_effect(PocketReactiveRuntime *runtime,
                                             const PocketReactiveHandle *dependencies,
                                             uint32_t dependency_count,
                                             PocketReactiveEffectFn effect,
                                             void *context,
                                             PocketReactiveSubscription *subscription) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!effect||!subscription||!dependency_count||
       !deps_valid(impl,dependencies,dependency_count))
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    for(uint32_t i=0;i<impl->effect_capacity;i++) {
        ReactiveEffect *record=&impl->effects[i];
        if(!record->live) {
            uint32_t generation=record->generation+1U;
            if(!generation)generation=1U;
            memset(record,0,sizeof(*record));record->generation=generation;record->live=1;
            record->dep_count=dependency_count;record->effect=effect;record->context=context;record->initial=1;
            for(uint32_t d=0;d<dependency_count;d++)record->deps[d]=dependencies[d];
            subscription->slot=i+1U;
            subscription->generation=generation;
            return POCKET_REACTIVE_OK;
        }
    }
    return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
}
PocketReactiveStatus pocket_reactive_remove_effect(PocketReactiveRuntime *runtime,
                                                    PocketReactiveSubscription subscription) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!subscription.slot||subscription.slot>impl->effect_capacity)
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    ReactiveEffect *effect=&impl->effects[subscription.slot-1U];
    if(!effect->live||effect->generation!=subscription.generation)
        return POCKET_REACTIVE_STALE_HANDLE;
    effect->live=0;
    return POCKET_REACTIVE_OK;
}
static int target_type_ok(PocketValueType type,PocketBindingTarget target) {
    if(target==POCKET_BIND_STYLE_REF||target==POCKET_BIND_TEXT_REF||
       target==POCKET_BIND_RESOURCE_REF||target==POCKET_BIND_STATES)
        return type==POCKET_VALUE_U64;
    if(target==POCKET_BIND_VISIBLE||target==POCKET_BIND_DISABLED)
        return type==POCKET_VALUE_BOOL;
    if(target==POCKET_BIND_VALUE)return type==POCKET_VALUE_I64;
    return 0;
}
PocketReactiveStatus pocket_reactive_bind_component(PocketReactiveRuntime *runtime,
                                                     PocketReactiveHandle source,
                                                     PocketComponentHandle component,
                                                     PocketBindingTarget target,
                                                     PocketReactiveSubscription *subscription) {
    ReactiveImpl *impl=impl_of(runtime);ReactiveNode *node=node_at(impl,source);
    if(!impl||!impl->components||!subscription||!node||!target_type_ok(node->type,target)||
       !pocket_component_handle_valid(component))
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    PocketComponentSnapshot snapshot;
    if(pocket_component_snapshot(impl->components,component,&snapshot)!=POCKET_COMPONENT_OK)
        return POCKET_REACTIVE_COMPONENT_ERROR;
    for(uint32_t i=0;i<impl->binding_capacity;i++) {
        ReactiveBinding *binding=&impl->bindings[i];
        if(!binding->live) {
            uint32_t generation=binding->generation+1U;
            if(!generation)generation=1U;
            memset(binding,0,sizeof(*binding));binding->generation=generation;binding->live=1;
            binding->source=source;binding->component=component;binding->target=target;binding->initial=1;
            subscription->slot=i+1U;
            subscription->generation=generation;
            return POCKET_REACTIVE_OK;
        }
    }
    return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
}
PocketReactiveStatus pocket_reactive_unbind(PocketReactiveRuntime *runtime,
                                            PocketReactiveSubscription subscription) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!subscription.slot||subscription.slot>impl->binding_capacity)
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    ReactiveBinding *binding=&impl->bindings[subscription.slot-1U];
    if(!binding->live||binding->generation!=subscription.generation)
        return POCKET_REACTIVE_STALE_HANDLE;
    binding->live=0;
    return POCKET_REACTIVE_OK;
}
size_t pocket_reactive_binding_count(const PocketReactiveRuntime *runtime) {
    const ReactiveImpl *impl=const_impl_of(runtime);size_t count=0;
    if(!impl)return 0;
    for(uint32_t i=0;i<impl->binding_capacity;i++)if(impl->bindings[i].live)count++;
    return count;
}
PocketReactiveStatus pocket_reactive_batch_begin(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||impl->transaction_active)return POCKET_REACTIVE_BUSY;
    if(impl->batch_depth==UINT32_MAX)return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    impl->batch_depth++;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_batch_end(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!impl->batch_depth)return POCKET_REACTIVE_BUSY;
    impl->batch_depth--;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_transaction_begin(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||impl->transaction_active||impl->batch_depth)return POCKET_REACTIVE_BUSY;
    impl->transaction_active=1;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_transaction_commit(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!impl->transaction_active)return POCKET_REACTIVE_BUSY;
    for(uint32_t i=0;i<impl->node_capacity;i++) {
        ReactiveNode *node=&impl->nodes[i];
        if(node->live&&node->kind==REACTIVE_NODE_SIGNAL&&node->staged) {
            apply_signal(impl,node,node->staged_value);node->staged=0;
        }
    }
    impl->transaction_active=0;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_reactive_transaction_rollback(PocketReactiveRuntime *runtime) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl||!impl->transaction_active)return POCKET_REACTIVE_BUSY;
    for(uint32_t i=0;i<impl->node_capacity;i++)impl->nodes[i].staged=0;
    impl->transaction_active=0;
    return POCKET_REACTIVE_OK;
}
static PocketReactiveStatus apply_binding(ReactiveImpl *impl,ReactiveBinding *binding,
                                          PocketReactiveValue value) {
    PocketComponentStatus status=POCKET_COMPONENT_INVALID_ARGUMENT;
    switch(binding->target) {
        case POCKET_BIND_STYLE_REF:
            status=pocket_component_set_style_ref(impl->components,binding->component,value.as.u64);break;
        case POCKET_BIND_TEXT_REF:
            status=pocket_component_set_text_ref(impl->components,binding->component,value.as.u64);break;
        case POCKET_BIND_RESOURCE_REF:
            status=pocket_component_set_resource_ref(impl->components,binding->component,value.as.u64);break;
        case POCKET_BIND_VISIBLE:
            status=pocket_component_set_visible(impl->components,binding->component,value.as.boolean);break;
        case POCKET_BIND_DISABLED:
            status=pocket_component_set_disabled(impl->components,binding->component,value.as.boolean);break;
        case POCKET_BIND_VALUE:
            if(value.as.i64<INT32_MIN||value.as.i64>INT32_MAX)return POCKET_REACTIVE_TYPE_MISMATCH;
            status=pocket_component_set_value(impl->components,binding->component,(int32_t)value.as.i64);break;
        case POCKET_BIND_STATES:
            if(value.as.u64>UINT32_MAX)return POCKET_REACTIVE_TYPE_MISMATCH;
            status=pocket_component_set_states(impl->components,binding->component,(uint32_t)value.as.u64);break;
        default:return POCKET_REACTIVE_INVALID_ARGUMENT;
    }
    if(status==POCKET_COMPONENT_STALE_HANDLE)return POCKET_REACTIVE_STALE_HANDLE;
    return status==POCKET_COMPONENT_OK?POCKET_REACTIVE_OK:POCKET_REACTIVE_COMPONENT_ERROR;
}
static int pending_computed(const ReactiveImpl *impl) {
    for(uint32_t i=0;i<impl->node_capacity;i++) {
        const ReactiveNode *node=&impl->nodes[i];
        if(node->live&&node->kind==REACTIVE_NODE_COMPUTED&&
           (node->dirty||dependency_changed(impl,node->deps,node->dep_versions,node->dep_count)))
            return 1;
    }
    return 0;
}
int pocket_reactive_has_pending(const PocketReactiveRuntime *runtime) {
    const ReactiveImpl *impl=const_impl_of(runtime);
    if(!impl)return 0;
    if(pending_computed(impl))return 1;
    for(uint32_t i=0;i<impl->effect_capacity;i++) {
        const ReactiveEffect *effect=&impl->effects[i];
        if(effect->live&&(effect->initial||
           dependency_changed(impl,effect->deps,effect->dep_versions,effect->dep_count)))
            return 1;
    }
    for(uint32_t i=0;i<impl->binding_capacity;i++) {
        const ReactiveBinding *binding=&impl->bindings[i];
        const ReactiveNode *source=binding->live?node_at_const(impl,binding->source):NULL;
        if(!binding->live) continue;
        PocketComponentSnapshot snapshot;
        if(!impl->components ||
           pocket_component_snapshot(impl->components,binding->component,&snapshot)!=POCKET_COMPONENT_OK)
            return 1;
        if(binding->initial||!source||source->version!=binding->source_version)
            return 1;
    }
    return 0;
}
PocketReactiveStatus pocket_reactive_flush(PocketReactiveRuntime *runtime,uint32_t budget,
                                            PocketReactiveFlushStats *stats) {
    ReactiveImpl *impl=impl_of(runtime);
    if(!impl)return POCKET_REACTIVE_INVALID_ARGUMENT;
    if(impl->batch_depth||impl->transaction_active)return POCKET_REACTIVE_BUSY;
    if(!budget)budget=impl->default_budget;
    PocketReactiveFlushStats local={0};
    for(uint32_t i=0;i<impl->effect_capacity;i++)impl->effects[i].runs_this_flush=0;
    while(pocket_reactive_has_pending(runtime)) {
        int progress=0;
        for(uint32_t i=0;i<impl->node_capacity;i++) {
            ReactiveNode *node=&impl->nodes[i];
            if(!node->live||node->kind!=REACTIVE_NODE_COMPUTED)continue;
            if(!node->dirty&&!dependency_changed(impl,node->deps,node->dep_versions,node->dep_count))
                continue;
            if(!computed_ready(impl,node))continue;
            if(local.work_items>=budget) {
                if(stats) *stats=local;
                return POCKET_REACTIVE_BUDGET_EXHAUSTED;
            }
            PocketReactiveValue values[POCKET_REACTIVE_MAX_DEPS];
            PocketReactiveStatus gathered=gather_values(impl,node->deps,node->dep_count,values);
            if(gathered!=POCKET_REACTIVE_OK) {
                if(stats) *stats=local;
                return gathered;
            }
            PocketReactiveValue out={node->type,{0}};
            PocketReactiveStatus status=node->compute(node->context,values,node->dep_count,&out);
            if(status!=POCKET_REACTIVE_OK) {
                if(stats) *stats=local;
                return status;
            }
            if(!value_valid(out)||out.type!=node->type) {
                if(stats) *stats=local;
                return POCKET_REACTIVE_TYPE_MISMATCH;
            }
            if(!node->version||!value_equal(node->value,out)) {
                node->value=out;node->version++;
                if(!node->version)node->version=1;
                impl->mutation_serial++;
            }
            snapshot_versions(impl,node->deps,node->dep_versions,node->dep_count);
            node->dirty=0;local.work_items++;local.computed_runs++;progress=1;
        }
        if(pending_computed(impl)&&!progress) {
            if(stats) *stats=local;
            return POCKET_REACTIVE_CYCLE;
        }
        for(uint32_t i=0;i<impl->effect_capacity;i++) {
            ReactiveEffect *effect=&impl->effects[i];
            if(!effect->live||(!effect->initial&&
               !dependency_changed(impl,effect->deps,effect->dep_versions,effect->dep_count)))
                continue;
            if(local.work_items>=budget) {
                if(stats) *stats=local;
                return POCKET_REACTIVE_BUDGET_EXHAUSTED;
            }
            if(++effect->runs_this_flush>REACTIVE_MAX_EFFECT_RUNS_PER_FLUSH) {
                if(stats) *stats=local;
                return POCKET_REACTIVE_CYCLE;
            }
            PocketReactiveValue values[POCKET_REACTIVE_MAX_DEPS];
            PocketReactiveStatus gathered=gather_values(impl,effect->deps,effect->dep_count,values);
            if(gathered!=POCKET_REACTIVE_OK) {
                if(stats) *stats=local;
                return gathered;
            }
            snapshot_versions(impl,effect->deps,effect->dep_versions,effect->dep_count);
            effect->initial=0;
            PocketReactiveStatus status=effect->effect(effect->context,values,effect->dep_count);
            local.work_items++;local.effect_runs++;progress=1;
            if(status!=POCKET_REACTIVE_OK) {
                if(stats) *stats=local;
                return status;
            }
        }
        for(uint32_t i=0;i<impl->binding_capacity;i++) {
            ReactiveBinding *binding=&impl->bindings[i];
            if(!binding->live)continue;
            const ReactiveNode *source=node_at_const(impl,binding->source);
            if(!source) {
                binding->live=0;local.work_items++;local.binding_cleanups++;progress=1;
                continue;
            }
            PocketComponentSnapshot snapshot;
            if(pocket_component_snapshot(impl->components,binding->component,&snapshot)!=POCKET_COMPONENT_OK) {
                binding->live=0;local.work_items++;local.binding_cleanups++;progress=1;
                continue;
            }
            if(!binding->initial&&source->version==binding->source_version)continue;
            if(local.work_items>=budget) {
                if(stats) *stats=local;
                return POCKET_REACTIVE_BUDGET_EXHAUSTED;
            }
            PocketReactiveStatus status=apply_binding(impl,binding,source->value);
            if(status==POCKET_REACTIVE_STALE_HANDLE) {
                binding->live=0;local.work_items++;local.binding_cleanups++;progress=1;
                continue;
            }
            if(status!=POCKET_REACTIVE_OK) {
                if(stats) *stats=local;
                return status;
            }
            binding->source_version=source->version;binding->initial=0;
            local.work_items++;local.binding_updates++;progress=1;
        }
        if(!progress&&pocket_reactive_has_pending(runtime)) {
            if(stats) *stats=local;
            return POCKET_REACTIVE_CYCLE;
        }
    }
    local.mutation_serial=impl->mutation_serial;
    if(stats)*stats=local;
    return POCKET_REACTIVE_OK;
}
