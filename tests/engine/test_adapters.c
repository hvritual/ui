#include "hosts/linux/engine/current.h"
#include "hosts/linux/engine/fake.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int opened;
    uint8_t pixels[8 * 8 * 4];
    uint32_t renders;
} CurrentFixture;

static PocketEngineStatus fixture_open(void *context, const PocketEngineOpenConfig *config) {
    CurrentFixture *f = context;
    if(!f || f->opened || config->width != 8U || config->height != 8U)
        return POCKET_ENGINE_BACKEND_FAILED;
    f->opened = 1;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fixture_close(void *context) {
    CurrentFixture *f = context;
    if(!f || !f->opened) return POCKET_ENGINE_BACKEND_FAILED;
    f->opened = 0;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fixture_tick(void *context, uint64_t now, uint32_t *deadline) {
    CurrentFixture *f = context;
    (void)now;
    if(!f || !f->opened || !deadline) return POCKET_ENGINE_BACKEND_FAILED;
    *deadline = 17U;
    return POCKET_ENGINE_OK;
}
static PocketEngineStatus fixture_render(void *context, PocketEngineFrame *out) {
    CurrentFixture *f = context;
    if(!f || !f->opened || !out) return POCKET_ENGINE_BACKEND_FAILED;
    ++f->renders;
    out->pixels = f->pixels;
    out->width = 8U; out->height = 8U; out->stride = 32U; out->length = sizeof(f->pixels);
    out->damage = (PocketEngineRect){1, 2, 3, 4};
    out->damage_valid = 1;
    return POCKET_ENGINE_OK;
}
static int expect(int ok, const char *name) {
    if(ok) return 1;
    fprintf(stderr, "FAIL %s\n", name);
    return 0;
}

static int test_fake(void) {
    PocketFakeEngine fake = {0};
    PocketEngineOpenConfig open = {8, 8, 1};
    PocketEngineCapabilities caps = 0;
    PocketEngineNode root = {0}, child = {0};
    PocketEngineNodeCreate create = {1, {0}};
    PocketEngineNodeUpdate update = {0};
    PocketEngineRect dirty = {1,1,2,2};
    PocketEngineFrame frame;
    uint32_t deadline = 0;
    uint8_t byte = 7;
    PocketEngineResource resource = {0};

    if(!expect(pocket_fake_engine_api.open(&fake, &open) == POCKET_ENGINE_OK, "fake-open")) return 0;
    if(!expect(pocket_fake_engine_api.query_capabilities(&fake, &caps) == POCKET_ENGINE_OK &&
               (caps & POCKET_ENGINE_CAP_NODE_TREE), "fake-caps")) return 0;
    if(!expect(pocket_fake_engine_api.node_create(&fake, &create, &root) == POCKET_ENGINE_OK &&
               pocket_engine_node_valid(root), "fake-root")) return 0;
    create.parent = root;
    if(!expect(pocket_fake_engine_api.node_create(&fake, &create, &child) == POCKET_ENGINE_OK,
               "fake-child")) return 0;
    update.fields = POCKET_ENGINE_UPDATE_BOUNDS | POCKET_ENGINE_UPDATE_CLIP |
                    POCKET_ENGINE_UPDATE_LAYER;
    update.bounds = (PocketEngineRect){0,0,8,8};
    update.clip = dirty; update.layer = 2; update.visible = 1; update.opacity_256 = 256;
    if(!expect(pocket_fake_engine_api.node_update(&fake, child, &update) == POCKET_ENGINE_OK,
               "fake-update")) return 0;
    if(!expect(pocket_fake_engine_api.request_layout(&fake, root) == POCKET_ENGINE_OK,
               "fake-layout")) return 0;
    if(!expect(pocket_fake_engine_api.invalidate(&fake, &dirty) == POCKET_ENGINE_OK,
               "fake-invalidate")) return 0;
    if(!expect(pocket_fake_engine_api.tick(&fake, 100, &deadline) == POCKET_ENGINE_OK &&
               deadline == 1000U, "fake-deadline")) return 0;
    if(!expect(pocket_fake_engine_api.render(&fake, &frame) == POCKET_ENGINE_OK &&
               frame.width == 8U && frame.damage_valid && frame.damage.x == 1,
               "fake-render")) return 0;
    if(!expect(pocket_fake_engine_api.resource_create(&fake, 1, &byte, 1, &resource) ==
               POCKET_ENGINE_OK && pocket_engine_resource_valid(resource), "fake-resource")) return 0;
    if(!expect(pocket_fake_engine_api.resource_release(&fake, resource) == POCKET_ENGINE_OK,
               "fake-resource-release")) return 0;
    if(!expect(pocket_fake_engine_api.resource_release(&fake, resource) == POCKET_ENGINE_STALE_HANDLE,
               "fake-stale-resource")) return 0;
    if(!expect(pocket_fake_engine_api.node_remove(&fake, child) == POCKET_ENGINE_OK,
               "fake-remove")) return 0;
    if(!expect(pocket_fake_engine_api.node_update(&fake, child, &update) == POCKET_ENGINE_STALE_HANDLE,
               "fake-stale-node")) return 0;
    if(!expect(pocket_fake_engine_api.close(&fake) == POCKET_ENGINE_OK, "fake-close")) return 0;
    return 1;
}
static int test_current(void) {
    CurrentFixture fixture = {0};
    PocketCurrentEngine current;
    PocketCurrentRendererHooks hooks = {
        &fixture, fixture_open, fixture_close, fixture_tick, fixture_render
    };
    PocketEngineOpenConfig open = {8,8,1};
    PocketEngineCapabilities caps = 0;
    PocketEngineFrame frame;
    PocketEngineNodeCreate create = {1,{0}};
    PocketEngineNode node = {0};
    uint32_t deadline = 0;

    pocket_current_engine_init(&current, &hooks);
    if(!expect(pocket_current_engine_api.open(&current, &open) == POCKET_ENGINE_OK, "current-open")) return 0;
    if(!expect(pocket_current_engine_api.query_capabilities(&current, &caps) == POCKET_ENGINE_OK &&
               (caps & POCKET_ENGINE_CAP_FRAME_RENDER) &&
               !(caps & POCKET_ENGINE_CAP_NODE_TREE), "current-caps")) return 0;
    if(!expect(pocket_current_engine_api.node_create(&current, &create, &node) ==
               POCKET_ENGINE_UNSUPPORTED, "current-explicit-unsupported")) return 0;
    if(!expect(pocket_current_engine_api.tick(&current, 100, &deadline) == POCKET_ENGINE_OK &&
               deadline == 17U, "current-tick")) return 0;
    if(!expect(pocket_current_engine_api.render(&current, &frame) == POCKET_ENGINE_OK &&
               frame.width == 8U && frame.damage_valid && fixture.renders == 1U,
               "current-render")) return 0;
    if(!expect(pocket_current_engine_api.close(&current) == POCKET_ENGINE_OK, "current-close")) return 0;
    if(!expect(pocket_current_engine_api.render(&current, &frame) ==
               POCKET_ENGINE_LIFECYCLE_ERROR, "current-after-close")) return 0;
    return 1;
}
static int test_backend_failure(void) {
    CurrentFixture fixture = {0};
    PocketCurrentEngine current;
    PocketCurrentRendererHooks hooks = {
        &fixture, fixture_open, fixture_close, fixture_tick, fixture_render
    };
    PocketEngineOpenConfig bad = {9,9,1};
    pocket_current_engine_init(&current, &hooks);
    return expect(pocket_current_engine_api.open(&current, &bad) ==
                  POCKET_ENGINE_BACKEND_FAILED, "backend-failure-contained");
}
int main(void) {
    if(!test_fake() || !test_current() || !test_backend_failure()) return 1;
    puts("ENGINE_ADAPTERS_OK");
    return 0;
}
