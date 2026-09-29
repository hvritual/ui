#ifndef POCKET_UI_COMPONENT_H
#define POCKET_UI_COMPONENT_H

#include "layout.h"
#include "style.h"
#include <stdint.h>

typedef enum {
    POCKET_COMPONENT_VIEW = 1,
    POCKET_COMPONENT_TEXT,
    POCKET_COMPONENT_IMAGE,
    POCKET_COMPONENT_ICON,
    POCKET_COMPONENT_BUTTON,
    POCKET_COMPONENT_ICON_BUTTON,
    POCKET_COMPONENT_TOGGLE,
    POCKET_COMPONENT_CHECKBOX,
    POCKET_COMPONENT_RADIO,
    POCKET_COMPONENT_SLIDER,
    POCKET_COMPONENT_PROGRESS,
    POCKET_COMPONENT_SPINNER,
    POCKET_COMPONENT_SCROLL,
    POCKET_COMPONENT_LIST,
    POCKET_COMPONENT_GRID,
    POCKET_COMPONENT_MODAL,
    POCKET_COMPONENT_DIALOG,
    POCKET_COMPONENT_TOAST,
    POCKET_COMPONENT_LOADING,
    POCKET_COMPONENT_ERROR_STATE,
    POCKET_COMPONENT_TEXT_FIELD,
    POCKET_COMPONENT_KIND_COUNT
} PocketComponentKind;

typedef enum {
    POCKET_COMPONENT_OK = 0,
    POCKET_COMPONENT_INVALID_ARGUMENT = 1,
    POCKET_COMPONENT_STALE_HANDLE = 2,
    POCKET_COMPONENT_RESOURCE_EXHAUSTED = 3,
    POCKET_COMPONENT_UI_ERROR = 4,
    POCKET_COMPONENT_DISABLED = 5,
    POCKET_COMPONENT_UNSUPPORTED = 6
} PocketComponentStatus;

typedef struct {
    uint32_t slot;
    uint32_t generation;
} PocketComponentHandle;

typedef enum {
    POCKET_COMPONENT_EVENT_ACTIVATE = 1,
    POCKET_COMPONENT_EVENT_VALUE_CHANGED = 2,
    POCKET_COMPONENT_EVENT_TOGGLE_CHANGED = 3,
    POCKET_COMPONENT_EVENT_FOCUS_REQUEST = 4,
    POCKET_COMPONENT_EVENT_SUBMIT = 5
} PocketComponentEventType;

typedef struct {
    PocketComponentEventType type;
    int32_t value;
    uint32_t states;
} PocketComponentEvent;

typedef void (*PocketComponentEventFn)(void *context, PocketComponentHandle component,
                                       const PocketComponentEvent *event);

typedef enum {
    POCKET_TEXT_INPUT_TEXT = 0,
    POCKET_TEXT_INPUT_PASSWORD = 1,
    POCKET_TEXT_INPUT_NUMBER = 2
} PocketTextInputKind;

typedef struct {
    uint64_t style_ref;
    uint64_t text_ref;
    uint64_t resource_ref;
    uint64_t group_id;
    uint64_t text_session_id;
    int32_t value;
    int32_t min_value;
    int32_t max_value;
    uint32_t states;
    uint32_t max_length;
    PocketTextInputKind input_kind;
    uint8_t visible;
    uint8_t disabled;
    uint8_t read_only;
    uint8_t secure;
} PocketComponentProps;

typedef struct {
    PocketComponentKind kind;
    PocketComponentHandle handle;
    PocketComponentHandle parent;
    PocketUiHandle root;
    PocketComponentProps props;
    uint32_t child_count;
} PocketComponentSnapshot;

typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketStyleRuntime *styles;
    uint32_t capacity;
} PocketComponentRuntimeConfig;

typedef struct { void *impl; } PocketComponentRuntime;

PocketComponentProps pocket_component_props_default(PocketComponentKind kind);
uint64_t pocket_component_default_style_ref(PocketComponentKind kind);
int pocket_component_handle_valid(PocketComponentHandle handle);

PocketComponentStatus pocket_component_runtime_init(PocketComponentRuntime *runtime,
                                                     const PocketComponentRuntimeConfig *config);
void pocket_component_runtime_dispose(PocketComponentRuntime *runtime);

PocketComponentStatus pocket_component_create(PocketComponentRuntime *runtime,
                                               PocketComponentKind kind,
                                               PocketComponentHandle parent,
                                               const PocketComponentProps *props,
                                               PocketComponentHandle *out);
PocketComponentStatus pocket_component_destroy(PocketComponentRuntime *runtime,
                                                PocketComponentHandle component);
PocketComponentStatus pocket_component_snapshot(const PocketComponentRuntime *runtime,
                                                 PocketComponentHandle component,
                                                 PocketComponentSnapshot *out);
size_t pocket_component_live_count(const PocketComponentRuntime *runtime);

PocketComponentStatus pocket_component_set_layout(PocketComponentRuntime *runtime,
                                                   PocketComponentHandle component,
                                                   const PocketLayoutSpec *spec);
PocketComponentStatus pocket_component_set_event_handler(PocketComponentRuntime *runtime,
                                                          PocketComponentHandle component,
                                                          PocketComponentEventFn handler,
                                                          void *context);
PocketComponentStatus pocket_component_set_states(PocketComponentRuntime *runtime,
                                                   PocketComponentHandle component,
                                                   uint32_t states);
PocketComponentStatus pocket_component_set_style_ref(PocketComponentRuntime *runtime,
                                                      PocketComponentHandle component,
                                                      uint64_t style_ref);
PocketComponentStatus pocket_component_set_text_ref(PocketComponentRuntime *runtime,
                                                     PocketComponentHandle component,
                                                     uint64_t text_ref);
PocketComponentStatus pocket_component_set_resource_ref(PocketComponentRuntime *runtime,
                                                         PocketComponentHandle component,
                                                         uint64_t resource_ref);
PocketComponentStatus pocket_component_set_visible(PocketComponentRuntime *runtime,
                                                    PocketComponentHandle component,
                                                    int visible);
PocketComponentStatus pocket_component_set_disabled(PocketComponentRuntime *runtime,
                                                     PocketComponentHandle component,
                                                     int disabled);
PocketComponentStatus pocket_component_set_value(PocketComponentRuntime *runtime,
                                                  PocketComponentHandle component,
                                                  int32_t value);
PocketComponentStatus pocket_component_activate(PocketComponentRuntime *runtime,
                                                 PocketComponentHandle component);
PocketComponentStatus pocket_component_submit(PocketComponentRuntime *runtime,
                                               PocketComponentHandle component);

#endif
