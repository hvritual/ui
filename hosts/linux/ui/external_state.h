#ifndef POCKET_UI_EXTERNAL_STATE_H
#define POCKET_UI_EXTERNAL_STATE_H

#include "reactive.h"
#include <stdint.h>

typedef struct {
    uint64_t field_id;
    PocketReactiveHandle signal;
} PocketExternalStateBinding;

typedef struct {
    PocketReactiveRuntime *runtime;
    PocketExternalStateBinding *bindings;
    uint32_t capacity;
    uint32_t count;
} PocketExternalStateAdapter;

PocketReactiveStatus pocket_external_state_init(PocketExternalStateAdapter *adapter,
                                                 PocketReactiveRuntime *runtime,
                                                 PocketExternalStateBinding *storage,
                                                 uint32_t capacity);
PocketReactiveStatus pocket_external_state_bind(PocketExternalStateAdapter *adapter,
                                                 uint64_t field_id,
                                                 PocketReactiveHandle signal);
PocketReactiveStatus pocket_external_state_ingest(PocketExternalStateAdapter *adapter,
                                                   uint64_t field_id,
                                                   PocketReactiveValue authoritative_value);

#endif
