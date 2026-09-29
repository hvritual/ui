#ifndef POCKET_UI_REACTIVE_H
#define POCKET_UI_REACTIVE_H

#include "component.h"
#include <stddef.h>
#include <stdint.h>

#define POCKET_REACTIVE_MAX_DEPS 16U

typedef enum {
    POCKET_REACTIVE_OK = 0,
    POCKET_REACTIVE_INVALID_ARGUMENT = 1,
    POCKET_REACTIVE_STALE_HANDLE = 2,
    POCKET_REACTIVE_RESOURCE_EXHAUSTED = 3,
    POCKET_REACTIVE_CYCLE = 4,
    POCKET_REACTIVE_BUDGET_EXHAUSTED = 5,
    POCKET_REACTIVE_BUSY = 6,
    POCKET_REACTIVE_TYPE_MISMATCH = 7,
    POCKET_REACTIVE_COMPONENT_ERROR = 8
} PocketReactiveStatus;

typedef enum {
    POCKET_VALUE_I64 = 1,
    POCKET_VALUE_U64 = 2,
    POCKET_VALUE_BOOL = 3
} PocketValueType;

typedef struct {
    PocketValueType type;
    union {
        int64_t i64;
        uint64_t u64;
        int boolean;
    } as;
} PocketReactiveValue;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketReactiveHandle;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketReactiveSubscription;

typedef PocketReactiveStatus (*PocketReactiveComputeFn)(
    void *context, const PocketReactiveValue *dependencies,
    uint32_t dependency_count, PocketReactiveValue *out);

typedef PocketReactiveStatus (*PocketReactiveEffectFn)(
    void *context, const PocketReactiveValue *dependencies,
    uint32_t dependency_count);

typedef enum {
    POCKET_BIND_STYLE_REF = 1,
    POCKET_BIND_TEXT_REF = 2,
    POCKET_BIND_RESOURCE_REF = 3,
    POCKET_BIND_VISIBLE = 4,
    POCKET_BIND_DISABLED = 5,
    POCKET_BIND_VALUE = 6,
    POCKET_BIND_STATES = 7
} PocketBindingTarget;

typedef struct {
    uint32_t node_capacity;
    uint32_t effect_capacity;
    uint32_t binding_capacity;
    uint32_t default_flush_budget;
    PocketComponentRuntime *components;
} PocketReactiveConfig;

typedef struct {
    uint32_t work_items;
    uint32_t computed_runs;
    uint32_t effect_runs;
    uint32_t binding_updates;
    uint32_t binding_cleanups;
    uint64_t mutation_serial;
} PocketReactiveFlushStats;

typedef struct { void *impl; } PocketReactiveRuntime;

PocketReactiveValue pocket_value_i64(int64_t value);
PocketReactiveValue pocket_value_u64(uint64_t value);
PocketReactiveValue pocket_value_bool(int value);
int pocket_reactive_handle_valid(PocketReactiveHandle handle);

PocketReactiveStatus pocket_reactive_init(PocketReactiveRuntime *runtime,
                                           const PocketReactiveConfig *config);
void pocket_reactive_dispose(PocketReactiveRuntime *runtime);

PocketReactiveStatus pocket_reactive_signal(PocketReactiveRuntime *runtime,
                                             PocketReactiveValue initial,
                                             PocketReactiveHandle *out);
PocketReactiveStatus pocket_reactive_computed(PocketReactiveRuntime *runtime,
                                               PocketValueType output_type,
                                               const PocketReactiveHandle *dependencies,
                                               uint32_t dependency_count,
                                               PocketReactiveComputeFn compute,
                                               void *context,
                                               PocketReactiveHandle *out);
PocketReactiveStatus pocket_reactive_rewire_computed(PocketReactiveRuntime *runtime,
                                                      PocketReactiveHandle computed,
                                                      const PocketReactiveHandle *dependencies,
                                                      uint32_t dependency_count);
PocketReactiveStatus pocket_reactive_get(const PocketReactiveRuntime *runtime,
                                          PocketReactiveHandle node,
                                          PocketReactiveValue *out);
PocketReactiveStatus pocket_reactive_set(PocketReactiveRuntime *runtime,
                                          PocketReactiveHandle signal,
                                          PocketReactiveValue value);
PocketReactiveStatus pocket_reactive_destroy(PocketReactiveRuntime *runtime,
                                              PocketReactiveHandle node);

PocketReactiveStatus pocket_reactive_effect(PocketReactiveRuntime *runtime,
                                             const PocketReactiveHandle *dependencies,
                                             uint32_t dependency_count,
                                             PocketReactiveEffectFn effect,
                                             void *context,
                                             PocketReactiveSubscription *subscription);
PocketReactiveStatus pocket_reactive_remove_effect(PocketReactiveRuntime *runtime,
                                                    PocketReactiveSubscription subscription);

PocketReactiveStatus pocket_reactive_bind_component(PocketReactiveRuntime *runtime,
                                                     PocketReactiveHandle source,
                                                     PocketComponentHandle component,
                                                     PocketBindingTarget target,
                                                     PocketReactiveSubscription *subscription);
PocketReactiveStatus pocket_reactive_unbind(PocketReactiveRuntime *runtime,
                                            PocketReactiveSubscription subscription);
size_t pocket_reactive_binding_count(const PocketReactiveRuntime *runtime);

PocketReactiveStatus pocket_reactive_batch_begin(PocketReactiveRuntime *runtime);
PocketReactiveStatus pocket_reactive_batch_end(PocketReactiveRuntime *runtime);
PocketReactiveStatus pocket_reactive_transaction_begin(PocketReactiveRuntime *runtime);
PocketReactiveStatus pocket_reactive_transaction_commit(PocketReactiveRuntime *runtime);
PocketReactiveStatus pocket_reactive_transaction_rollback(PocketReactiveRuntime *runtime);

PocketReactiveStatus pocket_reactive_flush(PocketReactiveRuntime *runtime,
                                            uint32_t budget,
                                            PocketReactiveFlushStats *stats);
int pocket_reactive_has_pending(const PocketReactiveRuntime *runtime);

#endif
