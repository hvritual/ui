#ifndef POCKET_UI_OVERLAY_H
#define POCKET_UI_OVERLAY_H

#include "component.h"
#include <stdint.h>

typedef enum {
    POCKET_OVERLAY_POPUP = 1,
    POCKET_OVERLAY_TOAST = 2,
    POCKET_OVERLAY_MODAL = 3,
    POCKET_OVERLAY_DIALOG = 4,
    POCKET_OVERLAY_LOADING = 5,
    POCKET_OVERLAY_KEYBOARD = 6,
    POCKET_OVERLAY_IME_CANDIDATE = 7
} PocketOverlayKind;

typedef enum {
    POCKET_OVERLAY_OK = 0,
    POCKET_OVERLAY_INVALID_ARGUMENT = 1,
    POCKET_OVERLAY_NOT_FOUND = 2,
    POCKET_OVERLAY_DUPLICATE = 3,
    POCKET_OVERLAY_FULL = 4,
    POCKET_OVERLAY_STALE_COMPONENT = 5
} PocketOverlayStatus;

typedef struct {
    uint64_t id;
    uint64_t owner_route;
    PocketOverlayKind kind;
    PocketComponentHandle root;
    uint64_t focus_token;
    uint8_t owns_root;
    uint8_t captures_input;
    uint8_t captures_focus;
    uint8_t dismiss_on_back;
} PocketOverlaySpec;

typedef struct {
    PocketOverlaySpec spec;
    uint64_t z_order;
    uint64_t previous_focus_token;
} PocketOverlaySnapshot;

typedef struct {
    PocketComponentRuntime *components;
    uint32_t capacity;
} PocketOverlayConfig;

typedef struct PocketOverlayManager { void *impl; } PocketOverlayManager;

PocketOverlayStatus pocket_overlay_init(PocketOverlayManager *manager,
                                        const PocketOverlayConfig *config);
void pocket_overlay_dispose(PocketOverlayManager *manager);
PocketOverlayStatus pocket_overlay_present(PocketOverlayManager *manager,
                                           const PocketOverlaySpec *spec);
PocketOverlayStatus pocket_overlay_dismiss(PocketOverlayManager *manager,uint64_t id);
uint32_t pocket_overlay_dismiss_owner(PocketOverlayManager *manager,uint64_t owner_route);
PocketOverlayStatus pocket_overlay_top(const PocketOverlayManager *manager,
                                       PocketOverlaySnapshot *out);
PocketOverlayStatus pocket_overlay_snapshot(const PocketOverlayManager *manager,uint64_t id,
                                            PocketOverlaySnapshot *out);
size_t pocket_overlay_count(const PocketOverlayManager *manager);
int pocket_overlay_blocks_background(const PocketOverlayManager *manager);
uint64_t pocket_overlay_focus_token(const PocketOverlayManager *manager);
PocketOverlayStatus pocket_overlay_back(PocketOverlayManager *manager,int *consumed);

/* Navigation owner-cleanup adapter. */
void pocket_overlay_navigation_cleanup(void *context,uint64_t route_id);

#endif
