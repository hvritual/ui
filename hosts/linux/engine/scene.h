#ifndef POCKET_ENGINE_SCENE_H
#define POCKET_ENGINE_SCENE_H
#include "contract.h"
/* Internal engine-neutral scene upload contract. Not an external package format.
 * resource_create copies synchronously; no caller-owned memory is retained. */
#define POCKET_SCENE_VERSION 1U
#define POCKET_SCENE_MAX_RECORDS 256U
#define POCKET_SCENE_RESOURCE 0x53434e31U
#define POCKET_SCENE_JSON_MAX 65536U

typedef struct {
    uint64_t id;
    uint32_t kind;
    PocketEngineRect bounds, clip;
    uint32_t background, foreground;
    uint16_t radius, opacity_256;
    uint64_t text_ref, resource_ref;
    int32_t value, minimum, maximum;
} PocketSceneRecord;

typedef struct {
    uint32_t version, width, height, count;
    uint32_t locale, background;
    int media_ready;
    PocketSceneRecord records[POCKET_SCENE_MAX_RECORDS];
} PocketScene;
#endif
