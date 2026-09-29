#include "external_state.h"

PocketReactiveStatus pocket_external_state_init(PocketExternalStateAdapter *adapter,
                                                 PocketReactiveRuntime *runtime,
                                                 PocketExternalStateBinding *storage,
                                                 uint32_t capacity) {
    if(!adapter||!runtime||!storage||!capacity)return POCKET_REACTIVE_INVALID_ARGUMENT;
    adapter->runtime=runtime;adapter->bindings=storage;adapter->capacity=capacity;adapter->count=0;
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_external_state_bind(PocketExternalStateAdapter *adapter,
                                                 uint64_t field_id,
                                                 PocketReactiveHandle signal) {
    if(!adapter||!field_id||!pocket_reactive_handle_valid(signal))
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    PocketReactiveValue value;
    if(pocket_reactive_get(adapter->runtime,signal,&value)!=POCKET_REACTIVE_OK)
        return POCKET_REACTIVE_STALE_HANDLE;
    for(uint32_t i=0;i<adapter->count;i++)
        if(adapter->bindings[i].field_id==field_id)return POCKET_REACTIVE_INVALID_ARGUMENT;
    if(adapter->count>=adapter->capacity)return POCKET_REACTIVE_RESOURCE_EXHAUSTED;
    adapter->bindings[adapter->count++]=(PocketExternalStateBinding){field_id,signal};
    return POCKET_REACTIVE_OK;
}
PocketReactiveStatus pocket_external_state_ingest(PocketExternalStateAdapter *adapter,
                                                   uint64_t field_id,
                                                   PocketReactiveValue authoritative_value) {
    if(!adapter||!field_id)return POCKET_REACTIVE_INVALID_ARGUMENT;
    for(uint32_t i=0;i<adapter->count;i++)
        if(adapter->bindings[i].field_id==field_id)
            return pocket_reactive_set(adapter->runtime,adapter->bindings[i].signal,authoritative_value);
    return POCKET_REACTIVE_STALE_HANDLE;
}
