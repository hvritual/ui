#ifndef POCKET_UI_MODEL_H
#define POCKET_UI_MODEL_H

#include "component.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    POCKET_MODEL_OK = 0,
    POCKET_MODEL_INVALID_ARGUMENT = 1,
    POCKET_MODEL_RESOURCE_EXHAUSTED = 2,
    POCKET_MODEL_STALE_COMPONENT = 3,
    POCKET_MODEL_DUPLICATE_KEY = 4,
    POCKET_MODEL_KEY_NOT_FOUND = 5,
    POCKET_MODEL_DELEGATE_ERROR = 6
} PocketModelStatus;

typedef uint32_t (*PocketModelCountFn)(void *context);
typedef int (*PocketModelKeyFn)(void *context,uint32_t index,uint64_t *key);
typedef PocketComponentStatus (*PocketModelBindFn)(
    void *context,uint32_t index,uint64_t key,
    PocketComponentRuntime *components,PocketComponentHandle component);

typedef struct {
    void *context;
    PocketModelCountFn count;
    PocketModelKeyFn key_at;
    PocketModelBindFn bind;
} PocketModelApi;

typedef struct {
    PocketComponentRuntime *components;
    PocketComponentHandle parent;
    PocketComponentKind item_kind;
    uint32_t max_items;
    PocketModelApi model;
} PocketRepeaterConfig;

typedef struct { void *impl; } PocketRepeater;

PocketModelStatus pocket_repeater_init(PocketRepeater *repeater,const PocketRepeaterConfig *config);
void pocket_repeater_dispose(PocketRepeater *repeater);
PocketModelStatus pocket_repeater_sync(PocketRepeater *repeater,uint64_t revision);
size_t pocket_repeater_count(const PocketRepeater *repeater);
uint64_t pocket_repeater_bind_calls(const PocketRepeater *repeater);

typedef struct {
    PocketComponentRuntime *components;
    PocketComponentHandle parent;
    PocketComponentKind item_kind;
    uint32_t overscan;
    uint32_t max_pool;
    PocketModelApi model;
} PocketVirtualCollectionConfig;

typedef struct {
    uint32_t model_count;
    uint32_t visible_first;
    uint32_t visible_count;
    uint32_t materialized_first;
    uint32_t materialized_count;
    uint32_t pool_size;
    uint32_t peak_pool_size;
    uint64_t bind_calls;
    uint64_t recycle_count;
    size_t pool_bytes;
    uint64_t selected_key;
    uint64_t focused_key;
    uint64_t revision;
} PocketVirtualCollectionStats;

typedef struct { void *impl; } PocketVirtualCollection;

PocketModelStatus pocket_virtual_collection_init(PocketVirtualCollection *collection,
                                                  const PocketVirtualCollectionConfig *config);
void pocket_virtual_collection_dispose(PocketVirtualCollection *collection);
PocketModelStatus pocket_virtual_collection_set_window(PocketVirtualCollection *collection,
                                                        uint32_t first,uint32_t visible_count);
PocketModelStatus pocket_virtual_collection_refresh(PocketVirtualCollection *collection,
                                                     uint64_t revision);
PocketModelStatus pocket_virtual_collection_select(PocketVirtualCollection *collection,uint64_t key);
PocketModelStatus pocket_virtual_collection_focus(PocketVirtualCollection *collection,uint64_t key);
PocketModelStatus pocket_virtual_collection_component_for_key(
    const PocketVirtualCollection *collection,uint64_t key,PocketComponentHandle *out);
PocketModelStatus pocket_virtual_collection_stats(const PocketVirtualCollection *collection,
                                                   PocketVirtualCollectionStats *out);

#endif
