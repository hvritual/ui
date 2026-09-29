#include "object.h"

#include <stdlib.h>
#include <string.h>

#define UI_DEFAULT_CAPACITY 32U
#define UI_DEFAULT_QUEUE_CAPACITY 256U
#define UI_DEFAULT_UPDATE_BUDGET 128U
#define UI_MAX_CAPACITY 65535U
#define UI_MAX_DEPTH 256U

typedef struct {
    uint32_t generation;
    int live;
    uint64_t stable_id;
    PocketUiNodeType type;
    PocketUiLifecyclePhase phase;
    PocketUiHandle parent;
    PocketUiHandle first_child;
    PocketUiHandle next_sibling;
    PocketUiHandle prev_sibling;
    uint32_t child_count;
    uint16_t depth;
    PocketUiProperties properties;
    PocketUiDirtyFlags dirty;
    PocketUiEventHandler event_handler;
    void *event_context;
    PocketEngineNode engine_node;
} UiNode;

typedef struct {
    PocketUiHandle node;
    PocketUiPropertyFields fields;
    PocketUiProperties properties;
} UiQueuedUpdate;

typedef struct {
    UiNode *nodes;
    uint32_t capacity;
    size_t live_count;
    uint64_t next_stable_id;

    UiQueuedUpdate *queue;
    uint32_t queue_capacity;
    uint32_t queue_head;
    uint32_t queue_count;
    uint32_t update_budget;

    const PocketEngineApi *engine_api;
    void *engine_context;
} UiImpl;

static PocketUiHandle zero_handle(void) {
    PocketUiHandle h = {0, 0};
    return h;
}
int pocket_ui_handle_valid(PocketUiHandle handle) {
    return handle.slot != 0U && handle.generation != 0U;
}
static UiImpl *impl(PocketUiTree *tree) {
    return tree ? (UiImpl *)tree->impl : NULL;
}
static const UiImpl *const_impl(const PocketUiTree *tree) {
    return tree ? (const UiImpl *)tree->impl : NULL;
}
static UiNode *find_node(UiImpl *ui, PocketUiHandle handle) {
    if(!ui || !pocket_ui_handle_valid(handle) || handle.slot > ui->capacity) return NULL;
    UiNode *n = &ui->nodes[handle.slot - 1U];
    return n->live && n->generation == handle.generation ? n : NULL;
}
static const UiNode *find_node_const(const UiImpl *ui, PocketUiHandle handle) {
    if(!ui || !pocket_ui_handle_valid(handle) || handle.slot > ui->capacity) return NULL;
    const UiNode *n = &ui->nodes[handle.slot - 1U];
    return n->live && n->generation == handle.generation ? n : NULL;
}
static PocketUiHandle handle_for(const UiImpl *ui, const UiNode *node) {
    PocketUiHandle h = {0,0};
    if(!ui || !node) return h;
    size_t index = (size_t)(node - ui->nodes);
    if(index >= ui->capacity) return h;
    h.slot = (uint32_t)index + 1U;
    h.generation = node->generation;
    return h;
}
static int type_valid(PocketUiNodeType type) {
    return type >= POCKET_UI_NODE && type <= POCKET_UI_CUSTOM;
}
static PocketUiProperties default_properties(void) {
    PocketUiProperties p;
    memset(&p, 0, sizeof(p));
    p.visible = 1;
    p.enabled = 1;
    p.opacity_256 = 256;
    return p;
}
static int grow_nodes(UiImpl *ui) {
    uint32_t next;
    UiNode *nodes;
    if(!ui || ui->capacity >= UI_MAX_CAPACITY) return 0;
    next = ui->capacity < 2U ? 2U : ui->capacity * 2U;
    if(next < ui->capacity || next > UI_MAX_CAPACITY) next = UI_MAX_CAPACITY;
    nodes = realloc(ui->nodes, (size_t)next * sizeof(*nodes));
    if(!nodes) return 0;
    memset(nodes + ui->capacity, 0, (size_t)(next - ui->capacity) * sizeof(*nodes));
    ui->nodes = nodes;
    ui->capacity = next;
    return 1;
}
static PocketUiStatus allocate_node(UiImpl *ui, UiNode **out, PocketUiHandle *handle) {
    if(!ui || !out || !handle) return POCKET_UI_INVALID_ARGUMENT;
    for(;;) {
        for(uint32_t i = 0; i < ui->capacity; ++i) {
            UiNode *n = &ui->nodes[i];
            if(!n->live) {
                uint32_t generation = n->generation + 1U;
                if(generation == 0U) generation = 1U;
                memset(n, 0, sizeof(*n));
                n->generation = generation;
                n->live = 1;
                n->properties = default_properties();
                handle->slot = i + 1U;
                handle->generation = generation;
                *out = n;
                return POCKET_UI_OK;
            }
        }
        if(!grow_nodes(ui)) return POCKET_UI_RESOURCE_EXHAUSTED;
    }
}
static void mark_ancestors(UiImpl *ui, PocketUiHandle parent, PocketUiDirtyFlags flags) {
    while(pocket_ui_handle_valid(parent)) {
        UiNode *p = find_node(ui, parent);
        if(!p) break;
        p->dirty |= flags;
        parent = p->parent;
    }
}
static void mark_dirty(UiImpl *ui, UiNode *node, PocketUiDirtyFlags self) {
    if(!ui || !node) return;
    node->dirty |= self;
    PocketUiDirtyFlags ancestors = 0;
    if(self & POCKET_UI_DIRTY_STRUCTURE)
        ancestors |= POCKET_UI_DIRTY_STRUCTURE | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    if(self & (POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_TEXT))
        ancestors |= POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    if(self & (POCKET_UI_DIRTY_STYLE | POCKET_UI_DIRTY_PAINT | POCKET_UI_DIRTY_RESOURCE))
        ancestors |= POCKET_UI_DIRTY_PAINT;
    if(ancestors) mark_ancestors(ui, node->parent, ancestors);
}
static PocketUiDirtyFlags dirty_for_fields(PocketUiPropertyFields fields) {
    PocketUiDirtyFlags flags = 0;
    if(fields & POCKET_UI_PROP_GEOMETRY) flags |= POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_VISIBLE) flags |= POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_ENABLED) flags |= POCKET_UI_DIRTY_INPUT | POCKET_UI_DIRTY_STYLE | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_OPACITY) flags |= POCKET_UI_DIRTY_STYLE | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_SEMANTIC_STATE) flags |= POCKET_UI_DIRTY_STYLE | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_STYLE_REF) flags |= POCKET_UI_DIRTY_STYLE | POCKET_UI_DIRTY_PAINT;
    if(fields & (POCKET_UI_PROP_FOCUSABLE | POCKET_UI_PROP_CLICKABLE)) flags |= POCKET_UI_DIRTY_INPUT;
    if(fields & POCKET_UI_PROP_RESOURCE) flags |= POCKET_UI_DIRTY_RESOURCE | POCKET_UI_DIRTY_PAINT;
    if(fields & POCKET_UI_PROP_TEXT) flags |= POCKET_UI_DIRTY_TEXT | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    return flags;
}
static int mounted_phase(PocketUiLifecyclePhase phase) {
    return phase == POCKET_UI_PHASE_MOUNTED || phase == POCKET_UI_PHASE_UPDATED ||
           phase == POCKET_UI_PHASE_LAYOUT || phase == POCKET_UI_PHASE_PAINT;
}
static PocketUiStatus engine_update(UiImpl *ui, UiNode *node,
                                    PocketUiPropertyFields fields,
                                    const PocketUiProperties *properties) {
    if(!ui->engine_api || !pocket_engine_node_valid(node->engine_node)) return POCKET_UI_OK;
    if(!(ui->engine_api->capabilities & POCKET_ENGINE_CAP_NODE_TREE) ||
       !ui->engine_api->node_update) return POCKET_UI_ENGINE_ERROR;

    PocketEngineNodeUpdate update;
    memset(&update, 0, sizeof(update));
    if(fields & POCKET_UI_PROP_GEOMETRY) {
        update.fields |= POCKET_ENGINE_UPDATE_BOUNDS;
        update.bounds.x = properties->geometry.x;
        update.bounds.y = properties->geometry.y;
        update.bounds.width = properties->geometry.width;
        update.bounds.height = properties->geometry.height;
    }
    if(fields & POCKET_UI_PROP_VISIBLE) {
        update.fields |= POCKET_ENGINE_UPDATE_VISIBLE;
        update.visible = properties->visible != 0;
    }
    if(fields & POCKET_UI_PROP_OPACITY) {
        update.fields |= POCKET_ENGINE_UPDATE_OPACITY;
        update.opacity_256 = properties->opacity_256;
    }
    if(!update.fields) return POCKET_UI_OK;
    return ui->engine_api->node_update(ui->engine_context, node->engine_node, &update) ==
           POCKET_ENGINE_OK ? POCKET_UI_OK : POCKET_UI_ENGINE_ERROR;
}
static void link_child(UiImpl *ui, UiNode *parent, PocketUiHandle child) {
    UiNode *c = find_node(ui, child);
    if(!parent || !c) return;
    c->parent = handle_for(ui, parent);
    c->next_sibling = parent->first_child;
    c->prev_sibling = zero_handle();
    if(pocket_ui_handle_valid(parent->first_child)) {
        UiNode *old = find_node(ui, parent->first_child);
        if(old) old->prev_sibling = child;
    }
    parent->first_child = child;
    parent->child_count++;
}
static void unlink_node(UiImpl *ui, UiNode *node) {
    if(!ui || !node || !pocket_ui_handle_valid(node->parent)) return;
    UiNode *parent = find_node(ui, node->parent);
    if(!parent) return;
    if(pocket_ui_handle_valid(node->prev_sibling)) {
        UiNode *prev = find_node(ui, node->prev_sibling);
        if(prev) prev->next_sibling = node->next_sibling;
    } else {
        parent->first_child = node->next_sibling;
    }
    if(pocket_ui_handle_valid(node->next_sibling)) {
        UiNode *next = find_node(ui, node->next_sibling);
        if(next) next->prev_sibling = node->prev_sibling;
    }
    if(parent->child_count) parent->child_count--;
    mark_dirty(ui, parent, POCKET_UI_DIRTY_STRUCTURE | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT);
    node->parent = node->next_sibling = node->prev_sibling = zero_handle();
}
static PocketUiStatus unmount_subtree(UiImpl *ui, UiNode *node) {
    PocketUiHandle child = node->first_child;
    while(pocket_ui_handle_valid(child)) {
        UiNode *c = find_node(ui, child);
        if(!c) break;
        PocketUiHandle next = c->next_sibling;
        PocketUiStatus status = unmount_subtree(ui, c);
        if(status != POCKET_UI_OK) return status;
        child = next;
    }
    if(mounted_phase(node->phase)) node->phase = POCKET_UI_PHASE_UNMOUNTED;
    return POCKET_UI_OK;
}
static PocketUiStatus destroy_subtree(UiImpl *ui, UiNode *node) {
    PocketUiStatus result = POCKET_UI_OK;
    PocketUiHandle child = node->first_child;
    while(pocket_ui_handle_valid(child)) {
        UiNode *c = find_node(ui, child);
        if(!c) break;
        PocketUiHandle next = c->next_sibling;
        PocketUiStatus status = destroy_subtree(ui, c);
        if(status != POCKET_UI_OK && result == POCKET_UI_OK) result = status;
        child = next;
    }

    if(mounted_phase(node->phase)) node->phase = POCKET_UI_PHASE_UNMOUNTED;
    if(ui->engine_api && pocket_engine_node_valid(node->engine_node) &&
       ui->engine_api->node_remove) {
        PocketEngineStatus status = ui->engine_api->node_remove(ui->engine_context, node->engine_node);
        if(status != POCKET_ENGINE_OK && result == POCKET_UI_OK) result = POCKET_UI_ENGINE_ERROR;
    }

    unlink_node(ui, node);
    node->first_child = zero_handle();
    node->child_count = 0;
    node->live = 0;
    node->phase = POCKET_UI_PHASE_UNMOUNTED;
    node->event_handler = NULL;
    node->event_context = NULL;
    node->engine_node = (PocketEngineNode){0};
    if(ui->live_count) ui->live_count--;
    return result;
}

PocketUiStatus pocket_ui_tree_init(PocketUiTree *tree, const PocketUiTreeConfig *config) {
    if(!tree || tree->impl) return POCKET_UI_INVALID_ARGUMENT;
    uint32_t capacity = config && config->initial_capacity ? config->initial_capacity : UI_DEFAULT_CAPACITY;
    uint32_t queue_capacity = config && config->update_queue_capacity ?
                              config->update_queue_capacity : UI_DEFAULT_QUEUE_CAPACITY;
    uint32_t budget = config && config->update_budget ? config->update_budget : UI_DEFAULT_UPDATE_BUDGET;
    if(capacity > UI_MAX_CAPACITY || !queue_capacity || !budget) return POCKET_UI_INVALID_ARGUMENT;
    if(config && config->engine_api && !pocket_engine_api_compatible(config->engine_api))
        return POCKET_UI_INVALID_ARGUMENT;

    UiImpl *ui = calloc(1, sizeof(*ui));
    if(!ui) return POCKET_UI_RESOURCE_EXHAUSTED;
    ui->nodes = calloc(capacity, sizeof(*ui->nodes));
    ui->queue = calloc(queue_capacity, sizeof(*ui->queue));
    if(!ui->nodes || !ui->queue) {
        free(ui->queue);
        free(ui->nodes);
        free(ui);
        return POCKET_UI_RESOURCE_EXHAUSTED;
    }
    ui->capacity = capacity;
    ui->queue_capacity = queue_capacity;
    ui->update_budget = budget;
    ui->next_stable_id = 1;
    if(config) {
        ui->engine_api = config->engine_api;
        ui->engine_context = config->engine_context;
    }
    tree->impl = ui;
    return POCKET_UI_OK;
}
void pocket_ui_tree_dispose(PocketUiTree *tree) {
    UiImpl *ui = impl(tree);
    if(!ui) return;
    for(uint32_t i = 0; i < ui->capacity; ++i) {
        UiNode *n = &ui->nodes[i];
        if(n->live && !pocket_ui_handle_valid(n->parent)) (void)destroy_subtree(ui, n);
    }
    free(ui->queue);
    free(ui->nodes);
    free(ui);
    tree->impl = NULL;
}
PocketUiStatus pocket_ui_create(PocketUiTree *tree, PocketUiNodeType type,
                                PocketUiHandle parent, PocketUiHandle *out) {
    UiImpl *ui = impl(tree);
    UiNode *node = NULL;
    PocketUiHandle handle = {0};
    PocketUiStatus status;
    if(!ui || !out || !type_valid(type)) return POCKET_UI_INVALID_ARGUMENT;

    /* Never retain an arena pointer across allocate_node(): it may realloc the
     * node table. Validate the parent handle first, then resolve it again after
     * the potentially-growing allocation. */
    if(pocket_ui_handle_valid(parent) && !find_node(ui, parent))
        return POCKET_UI_STALE_HANDLE;

    status = allocate_node(ui, &node, &handle);
    if(status != POCKET_UI_OK) return status;
    node->type = type;
    node->phase = POCKET_UI_PHASE_CREATED;

    UiNode *parent_node = pocket_ui_handle_valid(parent) ? find_node(ui, parent) : NULL;
    if(pocket_ui_handle_valid(parent) && !parent_node) {
        node->live = 0;
        return POCKET_UI_STALE_HANDLE;
    }
    node->depth = parent_node ? (uint16_t)(parent_node->depth + 1U) : 1U;
    if(node->depth > UI_MAX_DEPTH) {
        node->live = 0;
        return POCKET_UI_RESOURCE_EXHAUSTED;
    }
    if(ui->next_stable_id == UINT64_MAX) {
        node->live = 0;
        return POCKET_UI_RESOURCE_EXHAUSTED;
    }
    node->stable_id = ui->next_stable_id++;

    if(ui->engine_api && (ui->engine_api->capabilities & POCKET_ENGINE_CAP_NODE_TREE)) {
        if(!ui->engine_api->node_create) {
            node->live = 0;
            return POCKET_UI_ENGINE_ERROR;
        }
        PocketEngineNodeCreate create = {(uint32_t)type, {0}};
        if(parent_node) create.parent = parent_node->engine_node;
        PocketEngineStatus engine_status =
            ui->engine_api->node_create(ui->engine_context, &create, &node->engine_node);
        if(engine_status != POCKET_ENGINE_OK) {
            node->live = 0;
            node->engine_node = (PocketEngineNode){0};
            return POCKET_UI_ENGINE_ERROR;
        }
    }

    if(parent_node) {
        link_child(ui, parent_node, handle);
        /* link_child can only mutate existing slots; no allocation occurs, so
         * parent_node remains valid for this operation. */
        mark_dirty(ui, parent_node, POCKET_UI_DIRTY_STRUCTURE | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT);
    }
    node->dirty = POCKET_UI_DIRTY_STRUCTURE | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT;
    ui->live_count++;
    *out = handle;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_mount(PocketUiTree *tree, PocketUiHandle handle) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(node->phase != POCKET_UI_PHASE_CREATED && node->phase != POCKET_UI_PHASE_UNMOUNTED)
        return POCKET_UI_LIFECYCLE_ERROR;
    if(pocket_ui_handle_valid(node->parent)) {
        UiNode *parent = find_node(ui, node->parent);
        if(!parent || !mounted_phase(parent->phase)) return POCKET_UI_LIFECYCLE_ERROR;
    }
    node->phase = POCKET_UI_PHASE_MOUNTED;
    mark_dirty(ui, node, POCKET_UI_DIRTY_STRUCTURE | POCKET_UI_DIRTY_LAYOUT | POCKET_UI_DIRTY_PAINT);
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_update(PocketUiTree *tree, PocketUiHandle handle,
                                PocketUiPropertyFields fields,
                                const PocketUiProperties *properties) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!properties || !fields || (fields & ~POCKET_UI_PROP_ALL))
        return POCKET_UI_INVALID_ARGUMENT;
    if(!mounted_phase(node->phase)) return POCKET_UI_LIFECYCLE_ERROR;
    if((fields & POCKET_UI_PROP_GEOMETRY) &&
       (properties->geometry.width < 0 || properties->geometry.height < 0))
        return POCKET_UI_INVALID_ARGUMENT;
    if((fields & POCKET_UI_PROP_OPACITY) && properties->opacity_256 > 256U)
        return POCKET_UI_INVALID_ARGUMENT;

    PocketUiStatus engine_status = engine_update(ui, node, fields, properties);
    if(engine_status != POCKET_UI_OK) return engine_status;

    if(fields & POCKET_UI_PROP_GEOMETRY) node->properties.geometry = properties->geometry;
    if(fields & POCKET_UI_PROP_VISIBLE) node->properties.visible = properties->visible != 0;
    if(fields & POCKET_UI_PROP_ENABLED) node->properties.enabled = properties->enabled != 0;
    if(fields & POCKET_UI_PROP_OPACITY) node->properties.opacity_256 = properties->opacity_256;
    if(fields & POCKET_UI_PROP_SEMANTIC_STATE) node->properties.semantic_state = properties->semantic_state;
    if(fields & POCKET_UI_PROP_STYLE_REF) node->properties.style_ref = properties->style_ref;
    if(fields & POCKET_UI_PROP_FOCUSABLE) node->properties.focusable = properties->focusable != 0;
    if(fields & POCKET_UI_PROP_CLICKABLE) node->properties.clickable = properties->clickable != 0;
    node->phase = POCKET_UI_PHASE_UPDATED;
    mark_dirty(ui, node, dirty_for_fields(fields));
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_layout(PocketUiTree *tree, PocketUiHandle handle) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!mounted_phase(node->phase)) return POCKET_UI_LIFECYCLE_ERROR;
    node->dirty &= ~POCKET_UI_DIRTY_LAYOUT;
    node->phase = POCKET_UI_PHASE_LAYOUT;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_paint(PocketUiTree *tree, PocketUiHandle handle) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!mounted_phase(node->phase) || (node->dirty & POCKET_UI_DIRTY_LAYOUT))
        return POCKET_UI_LIFECYCLE_ERROR;
    node->dirty &= ~(POCKET_UI_DIRTY_PAINT | POCKET_UI_DIRTY_STYLE |
                     POCKET_UI_DIRTY_RESOURCE | POCKET_UI_DIRTY_TEXT);
    node->phase = POCKET_UI_PHASE_PAINT;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_unmount(PocketUiTree *tree, PocketUiHandle handle) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!mounted_phase(node->phase)) return POCKET_UI_LIFECYCLE_ERROR;
    return unmount_subtree(ui, node);
}
PocketUiStatus pocket_ui_destroy(PocketUiTree *tree, PocketUiHandle handle) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    return destroy_subtree(ui, node);
}
PocketUiStatus pocket_ui_snapshot(const PocketUiTree *tree, PocketUiHandle handle,
                                  PocketUiSnapshot *out) {
    const UiImpl *ui = const_impl(tree);
    const UiNode *node = find_node_const(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!out) return POCKET_UI_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->stable_id = node->stable_id;
    out->handle = handle;
    out->parent = node->parent;
    out->type = node->type;
    out->phase = node->phase;
    out->properties = node->properties;
    out->dirty = node->dirty;
    out->child_count = node->child_count;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_take_dirty(PocketUiTree *tree, PocketUiHandle handle,
                                    PocketUiDirtyFlags *out) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    if(!out) return POCKET_UI_INVALID_ARGUMENT;
    *out = node->dirty;
    node->dirty = 0;
    return POCKET_UI_OK;
}
size_t pocket_ui_live_count(const PocketUiTree *tree) {
    const UiImpl *ui = const_impl(tree);
    return ui ? ui->live_count : 0;
}
PocketUiStatus pocket_ui_set_event_handler(PocketUiTree *tree, PocketUiHandle handle,
                                           PocketUiEventHandler handler, void *context) {
    UiImpl *ui = impl(tree);
    UiNode *node = find_node(ui, handle);
    if(!node) return POCKET_UI_STALE_HANDLE;
    node->event_handler = handler;
    node->event_context = context;
    return POCKET_UI_OK;
}
static PocketUiStatus invoke_handler(UiNode *node, PocketUiHandle current,
                                     PocketUiEventPhase phase, PocketUiEvent *event) {
    if(!node->event_handler) return POCKET_UI_OK;
    event->current = current;
    event->phase = phase;
    PocketUiEventAction action = node->event_handler(node->event_context, event);
    if(action == POCKET_UI_EVENT_CONSUME) {
        event->consumed = 1;
    } else if(action == POCKET_UI_EVENT_CANCEL) {
        event->cancelled = 1;
        event->default_prevented = 1;
    } else if(action != POCKET_UI_EVENT_CONTINUE) {
        return POCKET_UI_INVALID_ARGUMENT;
    }
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_dispatch_event(PocketUiTree *tree, PocketUiHandle target,
                                        PocketUiEvent *event) {
    UiImpl *ui = impl(tree);
    if(!ui || !event) return POCKET_UI_INVALID_ARGUMENT;
    UiNode *target_node = find_node(ui, target);
    if(!target_node) return POCKET_UI_STALE_HANDLE;
    if(!mounted_phase(target_node->phase)) return POCKET_UI_LIFECYCLE_ERROR;

    /* Depth is capped at UI_MAX_DEPTH, so event dispatch never allocates.
     * This keeps touch/input handling deterministic on low-memory targets. */
    PocketUiHandle ancestors[UI_MAX_DEPTH];
    uint32_t count = 0;
    PocketUiHandle parent = target_node->parent;
    while(pocket_ui_handle_valid(parent)) {
        if(count >= UI_MAX_DEPTH) return POCKET_UI_LIFECYCLE_ERROR;
        UiNode *p = find_node(ui, parent);
        if(!p) return POCKET_UI_STALE_HANDLE;
        ancestors[count++] = parent;
        parent = p->parent;
    }

    event->target = target;
    event->current = zero_handle();
    event->consumed = event->cancelled = event->default_prevented = 0;
    PocketUiStatus result = POCKET_UI_OK;

    for(uint32_t i = count; i > 0 && !event->consumed && !event->cancelled; --i) {
        UiNode *n = find_node(ui, ancestors[i - 1U]);
        if(!n) return POCKET_UI_STALE_HANDLE;
        result = invoke_handler(n, ancestors[i - 1U], POCKET_UI_EVENT_CAPTURE, event);
        if(result != POCKET_UI_OK) return result;
    }
    if(!event->consumed && !event->cancelled) {
        /* A capture handler may create enough nodes to realloc the arena or
         * destroy the target. Resolve the handle again instead of retaining the
         * pre-dispatch pointer across user callbacks. */
        target_node = find_node(ui, target);
        if(!target_node) return POCKET_UI_STALE_HANDLE;
        result = invoke_handler(target_node, target, POCKET_UI_EVENT_TARGET, event);
        if(result != POCKET_UI_OK) return result;
    }
    for(uint32_t i = 0; i < count && !event->consumed && !event->cancelled; ++i) {
        UiNode *n = find_node(ui, ancestors[i]);
        if(!n) return POCKET_UI_STALE_HANDLE;
        result = invoke_handler(n, ancestors[i], POCKET_UI_EVENT_BUBBLE, event);
        if(result != POCKET_UI_OK) return result;
    }
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_enqueue_update(PocketUiTree *tree, PocketUiHandle node,
                                        PocketUiPropertyFields fields,
                                        const PocketUiProperties *properties) {
    UiImpl *ui = impl(tree);
    if(!ui || !properties || !fields || (fields & ~POCKET_UI_PROP_ALL))
        return POCKET_UI_INVALID_ARGUMENT;
    if(!find_node(ui, node)) return POCKET_UI_STALE_HANDLE;
    if(ui->queue_count >= ui->queue_capacity) return POCKET_UI_QUEUE_FULL;
    uint32_t tail = (ui->queue_head + ui->queue_count) % ui->queue_capacity;
    ui->queue[tail].node = node;
    ui->queue[tail].fields = fields;
    ui->queue[tail].properties = *properties;
    ui->queue_count++;
    return POCKET_UI_OK;
}
PocketUiStatus pocket_ui_drain_updates(PocketUiTree *tree, uint32_t budget,
                                       uint32_t *applied) {
    UiImpl *ui = impl(tree);
    if(!ui || !applied) return POCKET_UI_INVALID_ARGUMENT;
    if(!budget) budget = ui->update_budget;
    uint32_t processed = 0, successful = 0;
    PocketUiStatus first_error = POCKET_UI_OK;
    while(ui->queue_count && processed < budget) {
        UiQueuedUpdate update = ui->queue[ui->queue_head];
        ui->queue_head = (ui->queue_head + 1U) % ui->queue_capacity;
        ui->queue_count--;
        processed++;
        PocketUiStatus status = pocket_ui_update(tree, update.node, update.fields, &update.properties);
        if(status == POCKET_UI_OK) successful++;
        else if(first_error == POCKET_UI_OK) first_error = status;
    }
    *applied = successful;
    if(first_error != POCKET_UI_OK) return first_error;
    return ui->queue_count ? POCKET_UI_BUDGET_EXHAUSTED : POCKET_UI_OK;
}
size_t pocket_ui_queued_updates(const PocketUiTree *tree) {
    const UiImpl *ui = const_impl(tree);
    return ui ? ui->queue_count : 0;
}
