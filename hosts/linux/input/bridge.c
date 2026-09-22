#include "bridge.h"
#include <string.h>

_Static_assert(INPUT_RUNTIME_MAX_CONTACTS == POCKET_RUNTIME_MAX_CONTACTS,
               "input/runtime contact budgets must match");

static uint32_t active_mask(const InputBridge *bridge) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < INPUT_BRIDGE_MAX_IDS; ++i)
        if (bridge->active[i]) mask |= (uint32_t)1U << i;
    return mask;
}

static int has_cancel(const PocketRuntimeContactsInput *input, int id) {
    for (unsigned i = 0; i < input->cancelled_count; ++i)
        if (input->cancelled[i] == id) return 1;
    return 0;
}

static int build_current(const InputBridge *bridge,
                         PocketRuntimeContactsInput *out) {
    /* Active snapshot construction must not erase terminal cancellations
       already attached to this guest frame. Callers that need a fresh
       steady-state sample clear the whole structure before entering here. */
    out->contact_count = 0;
    memset(out->contacts, 0, sizeof(out->contacts));
    for (unsigned id = 0; id < INPUT_BRIDGE_MAX_IDS; ++id) {
        if (!bridge->active[id]) continue;
        if (out->contact_count >= POCKET_RUNTIME_MAX_CONTACTS) return 0;
        PocketRuntimeContact *contact = &out->contacts[out->contact_count++];
        contact->id = (int)id;
        contact->x = bridge->x[id];
        contact->y = bridge->y[id];
        contact->hit = bridge->hit[id];
    }
    return 1;
}

static int same_active_set(const PocketRuntimeContactsInput *a,
                           const PocketRuntimeContactsInput *b) {
    if (a->contact_count != b->contact_count ||
        a->cancelled_count || b->cancelled_count)
        return 0;
    for (unsigned i = 0; i < a->contact_count; ++i)
        if (a->contacts[i].id != b->contacts[i].id) return 0;
    return 1;
}

static InputBridgeFrame *queue_last(InputBridge *bridge) {
    if (!bridge->queue_count) return NULL;
    unsigned index =
        (bridge->queue_head + bridge->queue_count - 1U) % INPUT_BRIDGE_QUEUE;
    return &bridge->queue[index];
}

static int enqueue(InputBridge *bridge, const PocketRuntimeContactsInput *input,
                   uint64_t event_ns, int edge) {
    InputBridgeFrame *last = queue_last(bridge);
    if (!edge && last && !last->edge && same_active_set(&last->input, input)) {
        int retained_edge = last->edge;
        last->input = *input;
        last->event_ns = event_ns;
        last->edge = retained_edge;
        ++bridge->coalesced_frames;
        return 1;
    }
    if (bridge->queue_count >= INPUT_BRIDGE_QUEUE) {
        bridge->error = "INPUT_BRIDGE_QUEUE_OVERFLOW";
        return 0;
    }
    unsigned index =
        (bridge->queue_head + bridge->queue_count) % INPUT_BRIDGE_QUEUE;
    bridge->queue[index].input = *input;
    bridge->queue[index].event_ns = event_ns;
    bridge->queue[index].edge = edge;
    ++bridge->queue_count;
    if (bridge->queue_count > bridge->queue_peak)
        bridge->queue_peak = bridge->queue_count;
    return 1;
}

int input_bridge_init(InputBridge *bridge, InputHitTest hit_test,
                      void *hit_context) {
    if (!bridge || !hit_test) return 0;
    memset(bridge, 0, sizeof(*bridge));
    bridge->hit_test = hit_test;
    bridge->hit_context = hit_context;
    return 1;
}

int input_bridge_cancel_all(InputBridge *bridge, uint64_t event_ns) {
    if (!bridge || !bridge->hit_test) return 0;
    PocketRuntimeContactsInput terminal = {0};
    for (unsigned i = 0; i < bridge->delivered.contact_count; ++i)
        terminal.cancelled[terminal.cancelled_count++] = bridge->delivered.contacts[i].id;
    memset(bridge->active, 0, sizeof(bridge->active));
    memset(bridge->hit, 0, sizeof(bridge->hit));
    memset(bridge->queue, 0, sizeof(bridge->queue));
    bridge->queue_head = bridge->queue_count = 0;
    bridge->error = NULL;
    bridge->last_event_ns = event_ns;
    ++bridge->resets;
    return enqueue(bridge, &terminal, event_ns, 1);
}

int input_bridge_ingest(InputBridge *bridge, const InputFrame *frame,
                        uint64_t event_ns) {
    unsigned seen[INPUT_BRIDGE_MAX_IDS] = {0};
    uint32_t before;
    PocketRuntimeContactsInput guest;

    if (!bridge || !frame || !bridge->hit_test ||
        frame->contact_count > INPUT_RUNTIME_MAX_CONTACTS ||
        frame->cancelled_count > INPUT_RUNTIME_MAX_CONTACTS)
        return 0;

    if (frame->syn_dropped || frame->suppressed) {
        ++bridge->ingested_frames;
        return input_bridge_cancel_all(bridge, event_ns);
    }
    /* Validate before changing the bridge or consulting the hit-test. */
    for (unsigned i = 0; i < frame->contact_count; ++i) {
        int id = frame->contacts[i].id;
        if (id < 0 || id >= INPUT_BRIDGE_MAX_IDS || seen[id] ||
            frame->contacts[i].x < 0 || frame->contacts[i].x > 1023 ||
            frame->contacts[i].y < 0 || frame->contacts[i].y > 1023) {
            bridge->error = "INPUT_BRIDGE_BAD_CONTACT"; return 0;
        }
        seen[id] = 1;
    }
    memset(seen, 0, sizeof(seen));
    before = active_mask(bridge);
    memset(&guest, 0, sizeof(guest));

    for (unsigned i = 0; i < frame->cancelled_count; ++i) {
        int id = frame->cancelled[i];
        if (id < 0 || id >= INPUT_BRIDGE_MAX_IDS ||
            has_cancel(&guest, id)) {
            bridge->error = "INPUT_BRIDGE_BAD_CANCEL";
            return 0;
        }
        guest.cancelled[guest.cancelled_count++] = id;
        bridge->active[id] = 0;
        bridge->hit[id] = 0;
    }

    for (unsigned i = 0; i < frame->contact_count; ++i) {
        const InputContact *contact = &frame->contacts[i];
        int id = contact->id;
        if (id < 0 || id >= INPUT_BRIDGE_MAX_IDS || seen[id] ||
            has_cancel(&guest, id)) {
            bridge->error = "INPUT_BRIDGE_BAD_CONTACT";
            return 0;
        }
        seen[id] = 1;
        if (!bridge->active[id]) {
            bridge->hit[id] =
                bridge->hit_test(bridge->hit_context,
                                 (float)contact->x, (float)contact->y);
            ++bridge->hit_queries;
        }
        bridge->active[id] = 1;
        bridge->x[id] = contact->x;
        bridge->y[id] = contact->y;
    }

    /* InputFrame is a complete active snapshot. Absence is a normal release,
       not a cancellation. */
    for (unsigned id = 0; id < INPUT_BRIDGE_MAX_IDS; ++id) {
        if (bridge->active[id] && !seen[id]) {
            bridge->active[id] = 0;
            bridge->hit[id] = 0;
        }
    }

    if (!build_current(bridge, &guest)) {
        bridge->error = "INPUT_BRIDGE_CONTACT_BUDGET";
        return 0;
    }

    uint32_t after = active_mask(bridge);
    int edge = before != after || guest.cancelled_count != 0 ||
               frame->syn_dropped != 0;
    bridge->last_event_ns = event_ns;
    ++bridge->ingested_frames;
    return enqueue(bridge, &guest, event_ns, edge);
}

int input_bridge_next(InputBridge *bridge, PocketRuntimeContactsInput *out,
                      uint64_t *event_ns) {
    if (!bridge || !out) return 0;

    if (bridge->queue_count) {
        InputBridgeFrame *queued = &bridge->queue[bridge->queue_head];
        *out = queued->input;
        if (event_ns) *event_ns = queued->event_ns;
        memset(queued, 0, sizeof(*queued));
        bridge->queue_head = (bridge->queue_head + 1U) % INPUT_BRIDGE_QUEUE;
        --bridge->queue_count;
    } else {
        memset(out, 0, sizeof(*out));
        if (!build_current(bridge, out)) {
            bridge->error = "INPUT_BRIDGE_CONTACT_BUDGET";
            return 0;
        }
        if (event_ns) *event_ns = bridge->last_event_ns;
    }
    bridge->delivered = *out;
    ++bridge->delivered_frames;
    return 1;
}
