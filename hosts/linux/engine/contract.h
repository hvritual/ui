#ifndef POCKET_ENGINE_CONTRACT_H
#define POCKET_ENGINE_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#define POCKET_ENGINE_ABI_MAJOR 1U
#define POCKET_ENGINE_ABI_MINOR 0U
#define POCKET_BACKEND_ABI_MAJOR 1U

typedef enum {
    POCKET_ENGINE_OK = 0,
    POCKET_ENGINE_UNSUPPORTED = 1,
    POCKET_ENGINE_INVALID_ARGUMENT = 2,
    POCKET_ENGINE_STALE_HANDLE = 3,
    POCKET_ENGINE_CAPABILITY_MISSING = 4,
    POCKET_ENGINE_LIFECYCLE_ERROR = 5,
    POCKET_ENGINE_BACKEND_FAILED = 6,
    POCKET_ENGINE_RESOURCE_EXHAUSTED = 7
} PocketEngineStatus;

typedef uint64_t PocketEngineCapabilities;
enum {
    POCKET_ENGINE_CAP_FRAME_RENDER    = 1ULL << 0,
    POCKET_ENGINE_CAP_DAMAGE          = 1ULL << 1,
    POCKET_ENGINE_CAP_TIMER_DEADLINE  = 1ULL << 2,
    POCKET_ENGINE_CAP_NODE_TREE       = 1ULL << 3,
    POCKET_ENGINE_CAP_LAYOUT          = 1ULL << 4,
    POCKET_ENGINE_CAP_INVALIDATION    = 1ULL << 5,
    POCKET_ENGINE_CAP_CLIP            = 1ULL << 6,
    POCKET_ENGINE_CAP_LAYER           = 1ULL << 7,
    POCKET_ENGINE_CAP_RESOURCE        = 1ULL << 8
};

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketEngineNode;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketEngineResource;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PocketEngineRect;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const uint8_t *pixels;
    size_t length;
    PocketEngineRect damage;
    int damage_valid;
} PocketEngineFrame;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t target_api_level;
} PocketEngineOpenConfig;

typedef struct {
    uint32_t kind;
    PocketEngineNode parent;
} PocketEngineNodeCreate;

enum {
    POCKET_ENGINE_UPDATE_BOUNDS  = 1U << 0,
    POCKET_ENGINE_UPDATE_VISIBLE = 1U << 1,
    POCKET_ENGINE_UPDATE_OPACITY = 1U << 2,
    POCKET_ENGINE_UPDATE_CLIP    = 1U << 3,
    POCKET_ENGINE_UPDATE_LAYER   = 1U << 4
};

typedef struct {
    uint32_t fields;
    PocketEngineRect bounds;
    PocketEngineRect clip;
    uint16_t opacity_256;
    int32_t layer;
    int visible;
} PocketEngineNodeUpdate;

typedef struct {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    const char *name;
    PocketEngineCapabilities capabilities;

    PocketEngineStatus (*open)(void *context, const PocketEngineOpenConfig *config);
    PocketEngineStatus (*close)(void *context);
    PocketEngineStatus (*query_capabilities)(void *context, PocketEngineCapabilities *out);

    PocketEngineStatus (*node_create)(void *context, const PocketEngineNodeCreate *create,
                                      PocketEngineNode *out);
    PocketEngineStatus (*node_update)(void *context, PocketEngineNode node,
                                      const PocketEngineNodeUpdate *update);
    PocketEngineStatus (*node_remove)(void *context, PocketEngineNode node);

    PocketEngineStatus (*request_layout)(void *context, PocketEngineNode root);
    PocketEngineStatus (*invalidate)(void *context, const PocketEngineRect *rect);
    PocketEngineStatus (*tick)(void *context, uint64_t monotonic_ms, uint32_t *next_deadline_ms);
    PocketEngineStatus (*render)(void *context, PocketEngineFrame *out);

    PocketEngineStatus (*resource_create)(void *context, uint32_t kind,
                                          const void *bytes, size_t length,
                                          PocketEngineResource *out);
    PocketEngineStatus (*resource_release)(void *context, PocketEngineResource resource);
} PocketEngineApi;

/* Platform backends are separate contracts. GUI engines consume them through
 * Runtime-owned adapters; applications never receive these implementation pointers. */
typedef struct {
    uint32_t abi_major;
    uint32_t struct_size;
    void *context;
    PocketEngineStatus (*present)(void *context, const PocketEngineFrame *frame);
} PocketDisplayBackend;

typedef struct {
    uint32_t abi_major;
    uint32_t struct_size;
    void *context;
    PocketEngineStatus (*poll)(void *context, uint32_t timeout_ms, uint32_t *events);
} PocketInputBackend;

typedef struct {
    uint32_t abi_major;
    uint32_t struct_size;
    void *context;
    PocketEngineStatus (*measure)(void *context, const char *utf8, size_t length,
                                  int32_t max_width, int32_t *width, int32_t *height);
} PocketTextBackend;

typedef struct {
    uint32_t abi_major;
    uint32_t struct_size;
    void *context;
    PocketEngineStatus (*read)(void *context, uint64_t asset_id,
                               const void **bytes, size_t *length);
    void (*release)(void *context, const void *bytes);
} PocketAssetBackend;

static inline int pocket_engine_node_valid(PocketEngineNode node) {
    return node.slot != 0U && node.generation != 0U;
}
static inline int pocket_engine_resource_valid(PocketEngineResource resource) {
    return resource.slot != 0U && resource.generation != 0U;
}

#endif
