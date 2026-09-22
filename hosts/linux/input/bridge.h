#ifndef COFFEE_LINUX_INPUT_BRIDGE_H
#define COFFEE_LINUX_INPUT_BRIDGE_H

#include "state.h"
#include "pocket_runtime.h"
#include <stdint.h>

#define INPUT_BRIDGE_MAX_IDS INPUT_HW_MAX_SLOTS
#define INPUT_BRIDGE_QUEUE 16

typedef int (*InputHitTest)(void *context, float x, float y);

typedef struct {
    PocketRuntimeContactsInput input;
    uint64_t event_ns;
    int edge;
} InputBridgeFrame;

typedef struct {
    int active[INPUT_BRIDGE_MAX_IDS];
    int x[INPUT_BRIDGE_MAX_IDS];
    int y[INPUT_BRIDGE_MAX_IDS];
    int hit[INPUT_BRIDGE_MAX_IDS];

    PocketRuntimeContactsInput delivered;
    uint64_t resets;
    InputBridgeFrame queue[INPUT_BRIDGE_QUEUE];
    unsigned queue_head;
    unsigned queue_count;

    InputHitTest hit_test;
    void *hit_context;

    uint64_t ingested_frames;
    uint64_t delivered_frames;
    uint64_t hit_queries;
    uint64_t coalesced_frames;
    uint64_t queue_peak;
    uint64_t last_event_ns;
    const char *error;
} InputBridge;

int input_bridge_init(InputBridge *bridge, InputHitTest hit_test, void *hit_context);
/* Focus loss, disconnect and queue failure discard stale edges; only contacts
   already delivered to the guest need terminal cancellation. */
int input_bridge_cancel_all(InputBridge *bridge, uint64_t event_ns);

/* Ingest one coherent InputFrame. Bounded queue preserves contact edges while
   coalescing move-only frames. Returns zero on a safety-budget violation. */
int input_bridge_ingest(InputBridge *bridge, const InputFrame *frame, uint64_t event_ns);

/* Produce the next guest frame. When the edge queue is empty, returns the latest
   active snapshot with no repeated cancellations. */
int input_bridge_next(InputBridge *bridge, PocketRuntimeContactsInput *out,
                      uint64_t *event_ns);

#endif
