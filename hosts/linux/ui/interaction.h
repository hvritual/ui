#ifndef POCKET_UI_INTERACTION_H
#define POCKET_UI_INTERACTION_H

#include "component.h"
#include "overlay.h"
#include <stdint.h>

#define POCKET_INTERACTION_MAX_POINTERS 8U

typedef enum {
    POCKET_INTERACTION_OK = 0,
    POCKET_INTERACTION_INVALID_ARGUMENT = 1,
    POCKET_INTERACTION_STALE_HANDLE = 2,
    POCKET_INTERACTION_NO_TARGET = 3,
    POCKET_INTERACTION_POINTER_BUSY = 4,
    POCKET_INTERACTION_POINTER_NOT_ACTIVE = 5,
    POCKET_INTERACTION_CAPTURE_ERROR = 6,
    POCKET_INTERACTION_FOCUS_REJECTED = 7
} PocketInteractionStatus;

typedef enum {
    POCKET_POINTER_DOWN = 1,
    POCKET_POINTER_MOVE = 2,
    POCKET_POINTER_UP = 3,
    POCKET_POINTER_CANCEL = 4
} PocketPointerPhase;

typedef enum {
    POCKET_KEY_ACTION_BACK = 1,
    POCKET_KEY_ACTION_ACCEPT = 2,
    POCKET_KEY_ACTION_NEXT = 3,
    POCKET_KEY_ACTION_PREVIOUS = 4,
    POCKET_KEY_ACTION_UP = 5,
    POCKET_KEY_ACTION_DOWN = 6,
    POCKET_KEY_ACTION_LEFT = 7,
    POCKET_KEY_ACTION_RIGHT = 8
} PocketKeyAction;

enum {
    POCKET_UI_EVENT_POINTER_DOWN = 1001,
    POCKET_UI_EVENT_POINTER_MOVE = 1002,
    POCKET_UI_EVENT_POINTER_UP = 1003,
    POCKET_UI_EVENT_POINTER_CANCEL = 1004,
    POCKET_UI_EVENT_KEY_ACTION = 1010,
    POCKET_UI_EVENT_FOCUS_GAINED = 1020,
    POCKET_UI_EVENT_FOCUS_LOST = 1021
};

typedef struct {
    uint32_t pointer_id;
    PocketPointerPhase phase;
    int32_t x;
    int32_t y;
    uint64_t timestamp_ms;
} PocketPointerEvent;

typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketComponentRuntime *components;
    PocketOverlayManager *overlays;
    PocketUiHandle scene_root;
} PocketInteractionConfig;

typedef struct {
    PocketUiHandle focused;
    PocketUiHandle focus_scope;
    uint32_t active_pointers;
} PocketInteractionSnapshot;

typedef struct { void *impl; } PocketInteractionRuntime;

PocketInteractionStatus pocket_interaction_init(PocketInteractionRuntime *runtime,
                                                 const PocketInteractionConfig *config);
void pocket_interaction_dispose(PocketInteractionRuntime *runtime);

PocketInteractionStatus pocket_interaction_set_scene_root(PocketInteractionRuntime *runtime,
                                                           PocketUiHandle root);
PocketInteractionStatus pocket_interaction_hit_test(PocketInteractionRuntime *runtime,
                                                     int32_t x,int32_t y,
                                                     PocketUiHandle *out);
PocketInteractionStatus pocket_interaction_pointer(PocketInteractionRuntime *runtime,
                                                    const PocketPointerEvent *event);
PocketInteractionStatus pocket_interaction_capture(PocketInteractionRuntime *runtime,
                                                    uint32_t pointer_id,
                                                    PocketUiHandle target);
PocketInteractionStatus pocket_interaction_release_capture(PocketInteractionRuntime *runtime,
                                                            uint32_t pointer_id);
void pocket_interaction_cancel_all(PocketInteractionRuntime *runtime,uint64_t timestamp_ms);

PocketInteractionStatus pocket_interaction_set_focus_scope(PocketInteractionRuntime *runtime,
                                                            PocketUiHandle scope);
PocketInteractionStatus pocket_interaction_focus(PocketInteractionRuntime *runtime,
                                                  PocketUiHandle target);
PocketInteractionStatus pocket_interaction_clear_focus(PocketInteractionRuntime *runtime);
PocketInteractionStatus pocket_interaction_key(PocketInteractionRuntime *runtime,
                                                PocketKeyAction action);
PocketInteractionStatus pocket_interaction_snapshot(const PocketInteractionRuntime *runtime,
                                                     PocketInteractionSnapshot *out);

#endif
