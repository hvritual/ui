#include "current.h"

#include <string.h>

void pocket_current_engine_init(PocketCurrentEngine *engine,
                                const PocketCurrentRendererHooks *hooks) {
    if(!engine) return;
    memset(engine, 0, sizeof(*engine));
    if(hooks) engine->hooks = *hooks;
}
static PocketEngineStatus current_open(void *context, const PocketEngineOpenConfig *config) {
    PocketCurrentEngine *e = context;
    PocketEngineStatus status;
    if(!e || !config || e->opened || !e->hooks.open || !e->hooks.close ||
       !e->hooks.render) return POCKET_ENGINE_INVALID_ARGUMENT;
    status = e->hooks.open(e->hooks.context, config);
    if(status == POCKET_ENGINE_OK) e->opened = 1;
    return status;
}
static PocketEngineStatus current_close(void *context) {
    PocketCurrentEngine *e = context;
    PocketEngineStatus status;
    if(!e || !e->opened || !e->hooks.close) return POCKET_ENGINE_LIFECYCLE_ERROR;
    status = e->hooks.close(e->hooks.context);
    if(status == POCKET_ENGINE_OK) e->opened = 0;
    return status;
}
static PocketEngineStatus current_caps(void *context, PocketEngineCapabilities *out) {
    PocketCurrentEngine *e = context;
    if(!e || !out || !e->opened) return POCKET_ENGINE_LIFECYCLE_ERROR;
    *out = pocket_current_engine_api.capabilities;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus unsupported_create(void *context, const PocketEngineNodeCreate *create,
                                             PocketEngineNode *out) {
    (void)context; (void)create; (void)out; return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus unsupported_update(void *context, PocketEngineNode node,
                                             const PocketEngineNodeUpdate *update) {
    (void)context; (void)node; (void)update; return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus unsupported_remove(void *context, PocketEngineNode node) {
    (void)context; (void)node; return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus unsupported_layout(void *context, PocketEngineNode node) {
    (void)context; (void)node; return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus unsupported_invalidate(void *context, const PocketEngineRect *rect) {
    (void)context; (void)rect; return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus current_tick(void *context, uint64_t monotonic_ms, uint32_t *next_deadline_ms) {
    PocketCurrentEngine *e = context;
    if(!e || !e->opened || !next_deadline_ms) return POCKET_ENGINE_LIFECYCLE_ERROR;
    if(!e->hooks.tick) return POCKET_ENGINE_UNSUPPORTED;
    return e->hooks.tick(e->hooks.context, monotonic_ms, next_deadline_ms);
}
static PocketEngineStatus current_render(void *context, PocketEngineFrame *out) {
    PocketCurrentEngine *e = context;
    if(!e || !e->opened || !out || !e->hooks.render) return POCKET_ENGINE_LIFECYCLE_ERROR;
    memset(out, 0, sizeof(*out));
    return e->hooks.render(e->hooks.context, out);
}
static PocketEngineStatus unsupported_resource_create(void *context, uint32_t kind,
                                                       const void *bytes, size_t length,
                                                       PocketEngineResource *out) {
    (void)context; (void)kind; (void)bytes; (void)length; (void)out;
    return POCKET_ENGINE_UNSUPPORTED;
}
static PocketEngineStatus unsupported_resource_release(void *context, PocketEngineResource resource) {
    (void)context; (void)resource; return POCKET_ENGINE_UNSUPPORTED;
}

const PocketEngineApi pocket_current_engine_api = {
    POCKET_ENGINE_ABI_MAJOR, POCKET_ENGINE_ABI_MINOR, sizeof(PocketEngineApi),
    "current-pocket-renderer",
    POCKET_ENGINE_CAP_FRAME_RENDER | POCKET_ENGINE_CAP_DAMAGE |
    POCKET_ENGINE_CAP_TIMER_DEADLINE,
    current_open, current_close, current_caps,
    unsupported_create, unsupported_update, unsupported_remove,
    unsupported_layout, unsupported_invalidate, current_tick, current_render,
    unsupported_resource_create, unsupported_resource_release
};
