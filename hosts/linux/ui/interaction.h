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
    POCKET_INTERACTION_FOCUS_REJECTED = 7,
    POCKET_INTERACTION_BUSY = 8,
    POCKET_INTERACTION_CLOCK_REVERSED = 9,
    POCKET_INTERACTION_BUDGET_EXHAUSTED = 10
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
    POCKET_UI_EVENT_FOCUS_LOST = 1021,
    POCKET_UI_EVENT_TAP = 1101,
    POCKET_UI_EVENT_DOUBLE_TAP = 1102,
    POCKET_UI_EVENT_LONG_PRESS = 1103,
    POCKET_UI_EVENT_PAN_BEGIN = 1110,
    POCKET_UI_EVENT_PAN_UPDATE = 1111,
    POCKET_UI_EVENT_PAN_END = 1112,
    POCKET_UI_EVENT_DRAG_BEGIN = 1120,
    POCKET_UI_EVENT_DRAG_UPDATE = 1121,
    POCKET_UI_EVENT_DRAG_END = 1122,
    POCKET_UI_EVENT_SCROLL_BEGIN = 1130,
    POCKET_UI_EVENT_SCROLL_UPDATE = 1131,
    POCKET_UI_EVENT_SCROLL_END = 1132,
    POCKET_UI_EVENT_FLICK = 1140,
    POCKET_UI_EVENT_GESTURE_CANCEL = 1141
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

enum {
    POCKET_GESTURE_TAP = 1U << 0,
    POCKET_GESTURE_DOUBLE_TAP = 1U << 1,
    POCKET_GESTURE_LONG_PRESS = 1U << 2,
    POCKET_GESTURE_PAN = 1U << 3,
    POCKET_GESTURE_DRAG = 1U << 4,
    POCKET_GESTURE_SCROLL_X = 1U << 5,
    POCKET_GESTURE_SCROLL_Y = 1U << 6,
    POCKET_GESTURE_FLICK = 1U << 7,
    POCKET_GESTURE_ALL = (1U << 8) - 1U
};
/* Fixed bounded recognizer budgets, logical pixels and monotonic milliseconds. */
#define POCKET_GESTURE_SLOP 12
#define POCKET_GESTURE_LONG_MS 500U
#define POCKET_GESTURE_DOUBLE_MS 300U
#define POCKET_GESTURE_FLICK_PPS 600
#define POCKET_GESTURE_MAX_BINDINGS 128U

typedef struct { void *impl; } PocketInteractionRuntime;

/* An explicit zero mask disables default recognition for this target.
 * Clickable nodes default to Tap; Scroll objects default to vertical Scroll.
 * Handlers may capture/release but may not recursively inject pointer/tick/key.
 */
PocketInteractionStatus pocket_interaction_set_gestures(PocketInteractionRuntime *runtime,
                                                         PocketUiHandle target, uint32_t mask);
PocketInteractionStatus pocket_interaction_tick(PocketInteractionRuntime *runtime,
                                                 uint64_t monotonic_ms);
uint32_t pocket_interaction_next_deadline(const PocketInteractionRuntime *runtime,
                                          uint64_t monotonic_ms);

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
