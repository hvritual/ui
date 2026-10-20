#ifndef POCKET_SCENE_RUNTIME_H
#define POCKET_SCENE_RUNTIME_H
#include "scene.h"
#include "drawing.h"
#include "../host.h"
#include "../package/loaded.h"
/* Private bridge from engine-neutral scene resources to the pinned runtime. */
typedef struct {
    LinuxHost host;
    const char *asset_root;
    const PuiLoadedPackage *package; /* retained by the owning Framework */
    uint32_t generation;
    int opened, ready;
    uint8_t previous[POCKET_SCENE_WIRE_MAX];
    size_t previous_length;
    uint64_t scene_uploads, scene_skips, scene_wire_bytes;
    struct { uint64_t id; uint32_t revision,width,height,color; } drawing[POCKET_DRAW_SURFACES];
    uint64_t drawing_uploads,drawing_points;
} PocketSceneEngine;
void pocket_scene_engine_init(PocketSceneEngine *engine,const char *asset_root);
void pocket_scene_engine_init_package(PocketSceneEngine *engine,const PuiLoadedPackage *package);
extern const PocketEngineApi pocket_scene_engine_api;
#endif
