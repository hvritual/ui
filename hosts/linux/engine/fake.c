#include "fake.h"

#include <stdlib.h>
#include <string.h>

static PocketEngineStatus fake_open(void *context, const PocketEngineOpenConfig *config) {
    PocketFakeEngine *e = context;
    size_t pixels;
    if(!e || !config || e->opened || !config->width || !config->height ||
       config->width > 4096U || config->height > 4096U) return POCKET_ENGINE_INVALID_ARGUMENT;
    /* Dimensions are capped above, so this multiplication is bounded to 64 MiB. */
    pixels = (size_t)config->width * (size_t)config->height * 4U;
    e->pixels = calloc(1, pixels);
    if(!e->pixels) return POCKET_ENGINE_RESOURCE_EXHAUSTED;
    e->pixels_length = pixels;
    e->width = config->width;
    e->height = config->height;
    e->capabilities = pocket_fake_engine_api.capabilities;
    e->next_deadline_ms = 1000U;
    e->opened = 1;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_close(void *context) {
    PocketFakeEngine *e = context;
    if(!e || !e->opened) return POCKET_ENGINE_LIFECYCLE_ERROR;
    free(e->pixels);
    e->pixels = NULL;
    e->pixels_length = 0;
    e->opened = 0;
    e->width = e->height = 0;
    e->damage_valid = 0;
    e->next_deadline_ms = 0;
    /* Keep generation counters across close/open so handles from an older
     * engine session can never become valid again when slots are reused. */
    for(uint32_t i = 0; i < POCKET_FAKE_ENGINE_MAX_NODES; ++i) {
        e->nodes[i].live = 0;
        e->nodes[i].kind = 0;
        e->nodes[i].parent = (PocketEngineNode){0};
        memset(&e->nodes[i].update, 0, sizeof(e->nodes[i].update));
    }
    for(uint32_t i = 0; i < POCKET_FAKE_ENGINE_MAX_RESOURCES; ++i) {
        e->resources[i].live = 0;
        e->resources[i].kind = 0;
        e->resources[i].length = 0;
    }
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_caps(void *context, PocketEngineCapabilities *out) {
    PocketFakeEngine *e = context;
    if(!e || !out || !e->opened) return POCKET_ENGINE_LIFECYCLE_ERROR;
    *out = e->capabilities;
    return POCKET_ENGINE_OK;
}
static PocketFakeNode *find_node(PocketFakeEngine *e, PocketEngineNode h) {
    if(!e || h.slot == 0U || h.slot > POCKET_FAKE_ENGINE_MAX_NODES) return NULL;
    PocketFakeNode *n = &e->nodes[h.slot - 1U];
    return n->live && n->generation == h.generation ? n : NULL;
}
static PocketEngineStatus fake_node_create(void *context, const PocketEngineNodeCreate *create,
                                           PocketEngineNode *out) {
    PocketFakeEngine *e = context;
    if(!e || !create || !out || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    if(pocket_engine_node_valid(create->parent) && !find_node(e, create->parent))
        return POCKET_ENGINE_STALE_HANDLE;
    for(uint32_t i = 0; i < POCKET_FAKE_ENGINE_MAX_NODES; ++i) {
        PocketFakeNode *n = &e->nodes[i];
        if(!n->live) {
            uint32_t generation = n->generation + 1U;
            if(generation == 0U) generation = 1U;
            memset(n, 0, sizeof(*n));
            n->generation = generation;
            n->kind = create->kind;
            n->parent = create->parent;
            n->live = 1;
            out->slot = i + 1U;
            out->generation = generation;
            return POCKET_ENGINE_OK;
        }
    }
    return POCKET_ENGINE_RESOURCE_EXHAUSTED;
}
static PocketEngineStatus fake_node_update(void *context, PocketEngineNode node,
                                           const PocketEngineNodeUpdate *update) {
    PocketFakeEngine *e = context;
    PocketFakeNode *n;
    if(!e || !update || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    n = find_node(e, node);
    if(!n) return POCKET_ENGINE_STALE_HANDLE;
    if((update->fields & POCKET_ENGINE_UPDATE_CLIP) &&
       !(e->capabilities & POCKET_ENGINE_CAP_CLIP))
        return POCKET_ENGINE_CAPABILITY_MISSING;
    if((update->fields & POCKET_ENGINE_UPDATE_LAYER) &&
       !(e->capabilities & POCKET_ENGINE_CAP_LAYER))
        return POCKET_ENGINE_CAPABILITY_MISSING;
    n->update = *update;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_node_remove(void *context, PocketEngineNode node) {
    PocketFakeEngine *e = context;
    PocketFakeNode *n;
    if(!e || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    n = find_node(e, node);
    if(!n) return POCKET_ENGINE_STALE_HANDLE;
    n->live = 0;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_layout(void *context, PocketEngineNode root) {
    PocketFakeEngine *e = context;
    if(!e || !e->opened) return POCKET_ENGINE_LIFECYCLE_ERROR;
    return find_node(e, root) ? POCKET_ENGINE_OK : POCKET_ENGINE_STALE_HANDLE;
}
static PocketEngineStatus fake_invalidate(void *context, const PocketEngineRect *rect) {
    PocketFakeEngine *e = context;
    if(!e || !rect || !e->opened || rect->width <= 0 || rect->height <= 0)
        return POCKET_ENGINE_INVALID_ARGUMENT;
    e->damage = *rect;
    e->damage_valid = 1;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_tick(void *context, uint64_t monotonic_ms, uint32_t *next_deadline_ms) {
    PocketFakeEngine *e = context;
    (void)monotonic_ms;
    if(!e || !next_deadline_ms || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    *next_deadline_ms = e->next_deadline_ms;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fake_render(void *context, PocketEngineFrame *out) {
    PocketFakeEngine *e = context;
    if(!e || !out || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->pixels = e->pixels;
    out->width = e->width;
    out->height = e->height;
    out->stride = e->width * 4U;
    out->length = e->pixels_length;
    out->damage = e->damage;
    out->damage_valid = e->damage_valid;
    e->damage_valid = 0;
    return POCKET_ENGINE_OK;
}
static PocketFakeResource *find_resource(PocketFakeEngine *e, PocketEngineResource h) {
    if(!e || h.slot == 0U || h.slot > POCKET_FAKE_ENGINE_MAX_RESOURCES) return NULL;
    PocketFakeResource *r = &e->resources[h.slot - 1U];
    return r->live && r->generation == h.generation ? r : NULL;
}
static PocketEngineStatus fake_resource_create(void *context, uint32_t kind,
                                                const void *bytes, size_t length,
                                                PocketEngineResource *out) {
    PocketFakeEngine *e = context;
    if(!e || !bytes || !length || !out || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    for(uint32_t i = 0; i < POCKET_FAKE_ENGINE_MAX_RESOURCES; ++i) {
        PocketFakeResource *r = &e->resources[i];
        if(!r->live) {
            uint32_t generation = r->generation + 1U;
            if(generation == 0U) generation = 1U;
            memset(r, 0, sizeof(*r));
            r->generation = generation;
            r->kind = kind;
            r->length = length;
            r->live = 1;
            out->slot = i + 1U;
            out->generation = generation;
            return POCKET_ENGINE_OK;
        }
    }
    return POCKET_ENGINE_RESOURCE_EXHAUSTED;
}
static PocketEngineStatus fake_resource_release(void *context, PocketEngineResource resource) {
    PocketFakeEngine *e = context;
    PocketFakeResource *r;
    if(!e || !e->opened) return POCKET_ENGINE_INVALID_ARGUMENT;
    r = find_resource(e, resource);
    if(!r) return POCKET_ENGINE_STALE_HANDLE;
    r->live = 0;
    return POCKET_ENGINE_OK;
}

const PocketEngineApi pocket_fake_engine_api = {
    POCKET_ENGINE_ABI_MAJOR, POCKET_ENGINE_ABI_MINOR, sizeof(PocketEngineApi),
    "fake-reference",
    POCKET_ENGINE_CAP_FRAME_RENDER | POCKET_ENGINE_CAP_DAMAGE |
    POCKET_ENGINE_CAP_TIMER_DEADLINE | POCKET_ENGINE_CAP_NODE_TREE |
    POCKET_ENGINE_CAP_LAYOUT | POCKET_ENGINE_CAP_INVALIDATION |
    POCKET_ENGINE_CAP_CLIP | POCKET_ENGINE_CAP_LAYER | POCKET_ENGINE_CAP_RESOURCE,
    fake_open, fake_close, fake_caps,
    fake_node_create, fake_node_update, fake_node_remove,
    fake_layout, fake_invalidate, fake_tick, fake_render,
    fake_resource_create, fake_resource_release
};
