#ifndef POCKET_UI_NAVIGATION_H
#define POCKET_UI_NAVIGATION_H

#include "component.h"
#include <stdint.h>

typedef enum {
    POCKET_NAV_OK = 0,
    POCKET_NAV_INVALID_ARGUMENT = 1,
    POCKET_NAV_EMPTY = 2,
    POCKET_NAV_ROOT = 3,
    POCKET_NAV_FULL = 4,
    POCKET_NAV_DUPLICATE_ROUTE = 5,
    POCKET_NAV_STALE_COMPONENT = 6,
    POCKET_NAV_RESOURCE_FULL = 7
} PocketNavigationStatus;

typedef enum {
    POCKET_NAV_PUSH = 1,
    POCKET_NAV_POP = 2,
    POCKET_NAV_REPLACE = 3,
    POCKET_NAV_RESET = 4
} PocketNavigationTransition;

typedef enum {
    POCKET_NAV_WILL_APPEAR = 1,
    POCKET_NAV_DID_APPEAR = 2,
    POCKET_NAV_WILL_DISAPPEAR = 3,
    POCKET_NAV_DID_DISAPPEAR = 4,
    POCKET_NAV_DESTROYED = 5
} PocketNavigationLifecycle;

typedef enum {
    POCKET_NAV_RESOURCE_TIMER = 1,
    POCKET_NAV_RESOURCE_TASK = 2,
    POCKET_NAV_RESOURCE_INPUT_CAPTURE = 3
} PocketNavigationResourceKind;

typedef void (*PocketNavigationLifecycleFn)(void *context,uint64_t route_id,
                                            PocketNavigationLifecycle lifecycle,
                                            PocketNavigationTransition transition);
typedef void (*PocketNavigationCancelFn)(void *context,PocketNavigationResourceKind kind);
typedef void (*PocketNavigationOwnerCleanupFn)(void *context,uint64_t route_id);

typedef struct {
    uint64_t route_id;
    PocketComponentHandle root;
    uint8_t owns_root;
} PocketNavigationPage;

typedef struct {
    PocketComponentRuntime *components;
    uint32_t capacity;
    PocketNavigationLifecycleFn lifecycle;
    void *lifecycle_context;
    PocketNavigationOwnerCleanupFn owner_cleanup;
    void *owner_cleanup_context;
} PocketNavigationConfig;

typedef struct { void *impl; } PocketNavigationStack;

PocketNavigationStatus pocket_navigation_init(PocketNavigationStack *stack,
                                               const PocketNavigationConfig *config);
void pocket_navigation_dispose(PocketNavigationStack *stack);
PocketNavigationStatus pocket_navigation_push(PocketNavigationStack *stack,
                                               const PocketNavigationPage *page);
PocketNavigationStatus pocket_navigation_pop(PocketNavigationStack *stack);
PocketNavigationStatus pocket_navigation_replace(PocketNavigationStack *stack,
                                                  const PocketNavigationPage *page);
PocketNavigationStatus pocket_navigation_reset(PocketNavigationStack *stack,
                                                const PocketNavigationPage *page);
PocketNavigationStatus pocket_navigation_back(PocketNavigationStack *stack,int *consumed);
PocketNavigationStatus pocket_navigation_top(const PocketNavigationStack *stack,
                                              PocketNavigationPage *out);
size_t pocket_navigation_count(const PocketNavigationStack *stack);
PocketNavigationStatus pocket_navigation_register_cancel(PocketNavigationStack *stack,
                                                          uint64_t route_id,
                                                          PocketNavigationResourceKind kind,
                                                          PocketNavigationCancelFn cancel,
                                                          void *context);

#endif
