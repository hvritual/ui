#ifndef POCKET_SCENE_RUNTIME_H
#define POCKET_SCENE_RUNTIME_H
#include "scene.h"
#include "../host.h"
/* Private bridge from engine-neutral scene resources to the pinned runtime. */
typedef struct {
    LinuxHost host;
    const char *asset_root;
    uint32_t generation;
    int opened, ready;
    uint8_t previous[POCKET_SCENE_WIRE_MAX];
    size_t previous_length;
    uint64_t scene_uploads, scene_skips, scene_wire_bytes;
} PocketSceneEngine;
void pocket_scene_engine_init(PocketSceneEngine *engine,const char *asset_root);
extern const PocketEngineApi pocket_scene_engine_api;
#endif
