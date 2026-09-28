#include "hosts/linux/engine/contract.h"

#include <stdio.h>

static int fail(const char *name) {
    fprintf(stderr, "FAIL %s\n", name);
    return 1;
}
int main(void) {
    PocketEngineNode none = {0};
    PocketEngineResource no_resource = {0};
    PocketDisplayBackend display = {POCKET_BACKEND_ABI_MAJOR, sizeof(PocketDisplayBackend), 0, 0};
    PocketInputBackend input = {POCKET_BACKEND_ABI_MAJOR, sizeof(PocketInputBackend), 0, 0};
    PocketTextBackend text = {POCKET_BACKEND_ABI_MAJOR, sizeof(PocketTextBackend), 0, 0};
    PocketAssetBackend asset = {POCKET_BACKEND_ABI_MAJOR, sizeof(PocketAssetBackend), 0, 0, 0};

    if(POCKET_ENGINE_ABI_MAJOR != 1U || POCKET_ENGINE_ABI_MINOR != 0U)
        return fail("abi-version");
    if(sizeof(PocketEngineApi) < sizeof(void *) * 8U)
        return fail("api-size");
    if(pocket_engine_node_valid(none) || pocket_engine_resource_valid(no_resource))
        return fail("zero-handle");
    if(display.abi_major != 1U || input.abi_major != 1U ||
       text.abi_major != 1U || asset.abi_major != 1U)
        return fail("backend-version");
    if(POCKET_ENGINE_CAP_FRAME_RENDER == POCKET_ENGINE_CAP_NODE_TREE ||
       POCKET_ENGINE_CAP_DAMAGE == POCKET_ENGINE_CAP_LAYOUT)
        return fail("capability-bits");

    puts("ENGINE_CONTRACT_OK");
    return 0;
}
