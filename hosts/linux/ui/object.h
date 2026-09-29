#ifndef POCKET_UI_OBJECT_H
#define POCKET_UI_OBJECT_H

#include "../engine/contract.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    POCKET_UI_OK = 0,
    POCKET_UI_INVALID_ARGUMENT = 1,
    POCKET_UI_STALE_HANDLE = 2,
    POCKET_UI_LIFECYCLE_ERROR = 3,
    POCKET_UI_RESOURCE_EXHAUSTED = 4,
    POCKET_UI_ENGINE_ERROR = 5,
    POCKET_UI_QUEUE_FULL = 6,
    POCKET_UI_BUDGET_EXHAUSTED = 7
} PocketUiStatus;

typedef enum {
    POCKET_UI_NODE = 1,
    POCKET_UI_COMPONENT = 2,
    POCKET_UI_CONTAINER = 3,
    POCKET_UI_TEXT = 4,
    POCKET_UI_IMAGE = 5,
    POCKET_UI_INPUT = 6,
    POCKET_UI_SCROLL = 7,
    POCKET_UI_CUSTOM = 8
} PocketUiNodeType;

typedef enum {
    POCKET_UI_PHASE_CREATED = 1,
    POCKET_UI_PHASE_MOUNTED = 2,
    POCKET_UI_PHASE_UPDATED = 3,
    POCKET_UI_PHASE_LAYOUT = 4,
    POCKET_UI_PHASE_PAINT = 5,
    POCKET_UI_PHASE_UNMOUNTED = 6
} PocketUiLifecyclePhase;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketUiHandle;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PocketUiRect;

enum {
    POCKET_UI_DIRTY_STRUCTURE = 1U << 0,
    POCKET_UI_DIRTY_LAYOUT    = 1U << 1,
    POCKET_UI_DIRTY_STYLE     = 1U << 2,
    POCKET_UI_DIRTY_PAINT     = 1U << 3,
    POCKET_UI_DIRTY_RESOURCE  = 1U << 4,
    POCKET_UI_DIRTY_TEXT      = 1U << 5,
    POCKET_UI_DIRTY_INPUT     = 1U << 6
};
typedef uint32_t PocketUiDirtyFlags;

enum {
    POCKET_UI_PROP_GEOMETRY       = 1U << 0,
    POCKET_UI_PROP_VISIBLE        = 1U << 1,
    POCKET_UI_PROP_ENABLED        = 1U << 2,
    POCKET_UI_PROP_OPACITY        = 1U << 3,
    POCKET_UI_PROP_SEMANTIC_STATE = 1U << 4,
    POCKET_UI_PROP_STYLE_REF      = 1U << 5,
    POCKET_UI_PROP_FOCUSABLE      = 1U << 6,
    POCKET_UI_PROP_CLICKABLE      = 1U << 7,
    POCKET_UI_PROP_RESOURCE       = 1U << 8,
    POCKET_UI_PROP_TEXT           = 1U << 9,
    POCKET_UI_PROP_ALL            = (1U << 10) - 1U
};
typedef uint32_t PocketUiPropertyFields;

typedef struct {
    PocketUiRect geometry;
    uint64_t semantic_state;
    uint64_t style_ref;
    uint16_t opacity_256;
    uint8_t visible;
    uint8_t enabled;
    uint8_t focusable;
    uint8_t clickable;
} PocketUiProperties;

typedef struct {
    uint64_t stable_id;
    PocketUiHandle handle;
    PocketUiHandle parent;
    PocketUiNodeType type;
    PocketUiLifecyclePhase phase;
    PocketUiProperties properties;
    PocketUiDirtyFlags dirty;
    uint32_t child_count;
} PocketUiSnapshot;

typedef enum {
    POCKET_UI_EVENT_CAPTURE = 1,
    POCKET_UI_EVENT_TARGET = 2,
    POCKET_UI_EVENT_BUBBLE = 3
} PocketUiEventPhase;

typedef enum {
    POCKET_UI_EVENT_CONTINUE = 0,
    POCKET_UI_EVENT_CONSUME = 1,
    POCKET_UI_EVENT_CANCEL = 2
} PocketUiEventAction;

typedef struct {
    uint32_t type;
    PocketUiHandle target;
    PocketUiHandle current;
    PocketUiEventPhase phase;
    int32_t x;
    int32_t y;
    uint64_t data;
    int consumed;
    int cancelled;
    int default_prevented;
    /* Numeric interaction payload; never user-entered text. */
    uint32_t pointer_id;
    uint64_t timestamp_ms;
    int32_t delta_x, delta_y;
    int32_t velocity_x, velocity_y;
} PocketUiEvent;

typedef PocketUiEventAction (*PocketUiEventHandler)(void *context, PocketUiEvent *event);

typedef struct {
    const PocketEngineApi *engine_api;
    void *engine_context;
    uint32_t initial_capacity;
    uint32_t update_queue_capacity;
    uint32_t update_budget;
} PocketUiTreeConfig;

typedef struct {
    void *impl;
} PocketUiTree;

int pocket_ui_handle_valid(PocketUiHandle handle);
PocketUiStatus pocket_ui_tree_init(PocketUiTree *tree, const PocketUiTreeConfig *config);
void pocket_ui_tree_dispose(PocketUiTree *tree);

PocketUiStatus pocket_ui_create(PocketUiTree *tree, PocketUiNodeType type,
                                PocketUiHandle parent, PocketUiHandle *out);
PocketUiStatus pocket_ui_mount(PocketUiTree *tree, PocketUiHandle node);
PocketUiStatus pocket_ui_update(PocketUiTree *tree, PocketUiHandle node,
                                PocketUiPropertyFields fields,
                                const PocketUiProperties *properties);
PocketUiStatus pocket_ui_layout(PocketUiTree *tree, PocketUiHandle node);
PocketUiStatus pocket_ui_paint(PocketUiTree *tree, PocketUiHandle node);
PocketUiStatus pocket_ui_unmount(PocketUiTree *tree, PocketUiHandle node);
PocketUiStatus pocket_ui_destroy(PocketUiTree *tree, PocketUiHandle node);

PocketUiStatus pocket_ui_snapshot(const PocketUiTree *tree, PocketUiHandle node,
                                  PocketUiSnapshot *out);
PocketUiStatus pocket_ui_take_dirty(PocketUiTree *tree, PocketUiHandle node,
                                    PocketUiDirtyFlags *out);
size_t pocket_ui_live_count(const PocketUiTree *tree);
/* Read-only structural traversal for Runtime-owned layout/inspection layers.
 * Sibling order is implementation order; callers that need application creation
 * order must normalize explicitly. */
PocketUiStatus pocket_ui_first_child(const PocketUiTree *tree, PocketUiHandle parent,
                                     PocketUiHandle *out);
PocketUiStatus pocket_ui_next_sibling(const PocketUiTree *tree, PocketUiHandle node,
                                      PocketUiHandle *out);

PocketUiStatus pocket_ui_set_event_handler(PocketUiTree *tree, PocketUiHandle node,
                                           PocketUiEventHandler handler, void *context);
PocketUiStatus pocket_ui_dispatch_event(PocketUiTree *tree, PocketUiHandle target,
                                        PocketUiEvent *event);

PocketUiStatus pocket_ui_enqueue_update(PocketUiTree *tree, PocketUiHandle node,
                                        PocketUiPropertyFields fields,
                                        const PocketUiProperties *properties);
PocketUiStatus pocket_ui_drain_updates(PocketUiTree *tree, uint32_t budget,
                                       uint32_t *applied);
size_t pocket_ui_queued_updates(const PocketUiTree *tree);

#endif
