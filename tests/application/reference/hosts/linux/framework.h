#ifndef POCKET_FRAMEWORK_RUNTIME_H
#define POCKET_FRAMEWORK_RUNTIME_H
#include "engine/scene_runtime.h"
#include "input/interaction_bridge.h"
#include "media/store.h"
#include "apps/coffee-framework/app.h"
#include <stdio.h>

typedef struct {
    CoffeeApp app;
    PocketSceneEngine engine;
    PocketInputInteractionBridge input;
    PocketScene scene;
    MediaStore media;
    PocketDisplayBackend display;
    PocketEngineFrame frame;
    uint64_t clock_ns, event_ns, ticks, presents, clean_skips, bytes_written;
    uint64_t last_media_ns, timestamp_clamps;
    uint64_t input_to_cpu_present_ns;
    uint64_t update_duration_ns, render_duration_ns, present_duration_ns, present_complete_ns;
    uint64_t motion_presents;
    int presented_scroll_x;
    unsigned presented_dragging, presented_settling;
    unsigned page_mask, modal_seen;
    int opened, valid_frame, frame_capturable;
    const char *error;
} PocketFramework;
int pocket_framework_open(PocketFramework *runtime,unsigned height,unsigned items,
                          const char *assets,const char *media_root,
                          const PocketDisplayBackend *display);
/* Input timestamps are preserved separately; dispatch clock never goes backwards.
 * The same function is used by the live driver and deterministic test replay. */
int pocket_framework_input(void *runtime,const InputFrame *frame,uint64_t event_ns);
void pocket_framework_disconnect(PocketFramework *runtime,uint64_t now_ns);
int pocket_framework_tick(PocketFramework *runtime,uint64_t now_ns,int force_present);
int pocket_framework_close(PocketFramework *runtime);
int pocket_framework_can_snapshot(const PocketFramework *runtime);
int pocket_framework_snapshot(const PocketFramework *runtime,const char *new_path);
#endif
