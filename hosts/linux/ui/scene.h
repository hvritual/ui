#ifndef POCKET_UI_SCENE_H
#define POCKET_UI_SCENE_H
#include "component.h"
#include "../engine/scene.h"
/* A projection borrows the semantic tree, not private renderer objects.
 * Append roots in paint order (page, then overlays). Hidden subtrees are absent.
 * Unsupported visual features fail explicitly rather than silently drifting. */
typedef struct {
    const PocketUiTree *tree;
    const PocketComponentRuntime *components;
    const PocketLayoutContext *layout;
    const PocketStyleRuntime *styles;
} PocketSceneSource;
int pocket_scene_begin(PocketScene *scene,uint32_t width,uint32_t height,
                       uint32_t locale,uint32_t background,int media_ready);
int pocket_scene_append(PocketScene *scene,const PocketSceneSource *source,
                        PocketUiHandle root);
#endif
