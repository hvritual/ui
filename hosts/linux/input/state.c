#include "state.h"
#include <limits.h>
#include <string.h>

static int map_axis(int raw, InputAxisRange range, unsigned extent, int invert, int *out) {
    if (!out || extent == 0 || extent > 1024 || range.maximum <= range.minimum) return 0;
    int64_t value = raw;
    if (value < range.minimum) value = range.minimum;
    if (value > range.maximum) value = range.maximum;
    int64_t span = (int64_t)range.maximum - range.minimum;
    int64_t offset = value - range.minimum;
    int64_t mapped = (offset * (int64_t)(extent - 1U) + span / 2) / span;
    if (invert) mapped = (int64_t)(extent - 1U) - mapped;
    if (mapped < 0 || mapped > INT_MAX) return 0;
    *out = (int)mapped;
    return 1;
}

int input_map_point(const InputTransform *t, int raw_x, int raw_y, int *x, int *y) {
    if (!t || !x || !y) return 0;
    if (!t->swap_xy) {
        return map_axis(raw_x, t->x, t->width, t->invert_x, x) &&
               map_axis(raw_y, t->y, t->height, t->invert_y, y);
    }
    return map_axis(raw_y, t->y, t->width, t->invert_x, x) &&
           map_axis(raw_x, t->x, t->height, t->invert_y, y);
}

static int valid_transform(const InputTransform *t) {
    return t && t->width > 0 && t->width <= 1024 &&
           t->height > 0 && t->height <= 1024 &&
           (t->swap_xy == 0 || t->swap_xy == 1) &&
           (t->invert_x == 0 || t->invert_x == 1) &&
           (t->invert_y == 0 || t->invert_y == 1) &&
           t->x.maximum > t->x.minimum && t->y.maximum > t->y.minimum;
}

static void clear_slots(InputState *s) {
    memset(s->slots, 0, sizeof(s->slots));
    for (unsigned i = 0; i < INPUT_HW_MAX_SLOTS; ++i)
        s->slots[i].tracking_id = -1;
    s->current_slot = 0;
    memset(&s->a_packet, 0, sizeof(s->a_packet));
    memset(s->a_contacts, 0, sizeof(s->a_contacts));
    s->a_fields = s->a_count = 0;
    s->a_touch_down = 0;
    s->legacy_down = 0;
    s->legacy_have_x = 0;
    s->legacy_have_y = 0;
}

int input_state_init(InputState *s, InputProtocol protocol, unsigned slot_count,
                     const InputTransform *transform) {
    if (!s || !valid_transform(transform)) return 0;
    if (protocol != INPUT_PROTOCOL_SINGLE && protocol != INPUT_PROTOCOL_MT_B &&
        protocol != INPUT_PROTOCOL_MT_A) return 0;
    if (protocol != INPUT_PROTOCOL_SINGLE &&
        (slot_count == 0 || slot_count > INPUT_HW_MAX_SLOTS)) return 0;
    memset(s, 0, sizeof(*s));
    s->protocol = protocol;
    s->slot_count = protocol == INPUT_PROTOCOL_SINGLE ? 1U : slot_count;
    s->transform = *transform;
    s->transform_valid = 1;
    clear_slots(s);
    return 1;
}

static void queue_cancel(InputState *s, int id) {
    for (unsigned i = 0; i < s->cancel_count; ++i)
        if (s->cancel_queue[i] == id) return;
    if (s->cancel_count < INPUT_RUNTIME_MAX_CONTACTS)
        s->cancel_queue[s->cancel_count++] = id;
}

static void cancel_published(InputState *s) {
    for (unsigned i = 0; i < s->slot_count; ++i) {
        if (s->slots[i].published) {
            queue_cancel(s, (int)i);
            s->slots[i].published = 0;
        }
    }
}

static unsigned active_count(const InputState *s) {
    unsigned count = 0;
    if (s->protocol == INPUT_PROTOCOL_SINGLE) return s->legacy_down ? 1U : 0U;
    for (unsigned i = 0; i < s->slot_count; ++i)
        if (s->slots[i].active) ++count;
    return count;
}

static void frame_begin(InputState *s, int dropped) {
    memset(&s->committed, 0, sizeof(s->committed));
    s->committed.sequence = ++s->sequence;
    s->committed.suppressed = s->suppress_until_all_up;
    s->committed.syn_dropped = dropped;
    for (unsigned i = 0; i < s->cancel_count; ++i)
        s->committed.cancelled[s->committed.cancelled_count++] = s->cancel_queue[i];
    s->cancel_count = 0;
}

static int commit_single(InputState *s) {
    frame_begin(s, 0);
    InputSlot *slot = &s->slots[0];
    if (s->suppress_until_all_up) {
        if (!s->legacy_down) {
            s->suppress_until_all_up = 0;
            s->overflowed = 0;
            s->committed.suppressed = 0;
        }
        slot->published = 0;
        return 1;
    }
    if (!s->legacy_down) {
        slot->published = 0;
        return 1;
    }
    if (!s->legacy_have_x || !s->legacy_have_y) return 1;
    int x, y;
    if (!input_map_point(&s->transform, s->legacy_raw_x, s->legacy_raw_y, &x, &y))
        return 0;
    s->committed.contacts[0] = (InputContact){.id = 0, .x = x, .y = y};
    s->committed.contact_count = 1;
    slot->published = 1;
    return 1;
}

static int commit_mt(InputState *s) {
    unsigned active = active_count(s);
    if (active > INPUT_RUNTIME_MAX_CONTACTS) {
        cancel_published(s);
        s->suppress_until_all_up = 1;
        s->overflowed = 1;
    }
    frame_begin(s, 0);
    if (s->suppress_until_all_up) {
        for (unsigned i = 0; i < s->slot_count; ++i)
            s->slots[i].published = 0;
        if (active == 0) {
            s->suppress_until_all_up = 0;
            s->overflowed = 0;
            s->committed.suppressed = 0;
        }
        return 1;
    }
    for (unsigned i = 0; i < s->slot_count; ++i) {
        InputSlot *slot = &s->slots[i];
        if (!slot->active) {
            slot->published = 0;
            slot->defer_publish = 0;
            continue;
        }
        if (slot->defer_publish) {
            /* A new tracking id replaced a contact that the guest still saw
               in the previous frame. Emit only the cancellation this report;
               the replacement may enter on the next coherent report. */
            slot->published = 0;
            slot->defer_publish = 0;
            continue;
        }
        if (!slot->have_x || !slot->have_y) {
            slot->published = 0;
            continue;
        }
        if (s->committed.contact_count >= INPUT_RUNTIME_MAX_CONTACTS) return 0;
        int x, y;
        if (!input_map_point(&s->transform, slot->raw_x, slot->raw_y, &x, &y))
            return 0;
        InputContact *contact =
            &s->committed.contacts[s->committed.contact_count++];
        *contact = (InputContact){.id = (int)i, .x = x, .y = y};
        slot->published = 1;
    }
    return 1;
}

/* A packet's position is never inherited from another contact or frame. */
static int finish_a_packet(InputState *s) {
    if (!s->a_fields) return 1; /* legal empty SYN_MT_REPORT */
    if (s->a_fields == 1U && s->a_packet.tracking_id == -1) {
        s->a_fields = 0;
        memset(&s->a_packet, 0, sizeof(s->a_packet));
        return 1;
    }
    if (s->a_fields != 7U || s->a_packet.tracking_id < 0 ||
        s->a_count >= s->slot_count) return 0;
    for (unsigned i = 0; i < s->a_count; ++i)
        if (s->a_contacts[i].tracking_id == s->a_packet.tracking_id) return 0;
    s->a_packet.active = s->a_packet.have_x = s->a_packet.have_y = 1;
    s->a_contacts[s->a_count++] = s->a_packet;
    s->a_fields = 0;
    memset(&s->a_packet, 0, sizeof(s->a_packet));
    return 1;
}

static int commit_a(InputState *s) {
    if (!finish_a_packet(s)) return 0;
    /* A malformed empty report while BTN_TOUCH remains down is not an Up. */
    if ((s->a_count != 0) != (s->a_touch_down != 0)) return 0;
    InputSlot next[INPUT_HW_MAX_SLOTS] = {{0}};
    unsigned used[INPUT_HW_MAX_SLOTS] = {0};
    unsigned assigned[INPUT_HW_MAX_SLOTS] = {0};
    for (unsigned i = 0; i < s->slot_count; ++i) next[i].tracking_id = -1;
    /* Reserve continuing IDs first, regardless of packet order. */
    for (unsigned j = 0; j < s->a_count; ++j) {
        for (unsigned i = 0; i < s->slot_count; ++i) {
            if (s->slots[i].active &&
                s->slots[i].tracking_id == s->a_contacts[j].tracking_id) {
                next[i] = s->a_contacts[j];
                next[i].published = s->slots[i].published;
                used[i] = assigned[j] = 1;
                break;
            }
        }
    }
    for (unsigned j = 0; j < s->a_count; ++j) {
        if (assigned[j]) continue;
        unsigned i = 0;
        /* Prefer an unused old slot so a released ID gets a normal Up. */
        for (; i < s->slot_count; ++i)
            if (!used[i] && !s->slots[i].published) break;
        if (i == s->slot_count)
            for (i = 0; i < s->slot_count && used[i]; ++i) {}
        if (i == s->slot_count) return 0;
        next[i] = s->a_contacts[j];
        if (s->slots[i].published) {
            queue_cancel(s, (int)i);
            next[i].defer_publish = 1;
        }
        used[i] = 1;
    }
    /* Overflow cancels the previous frame before replacing its logical map. */
    if (s->a_count > INPUT_RUNTIME_MAX_CONTACTS) {
        cancel_published(s);
        s->suppress_until_all_up = s->overflowed = 1;
    }
    memcpy(s->slots, next, sizeof(next));
    s->a_count = 0;
    return commit_mt(s);
}

static int feed_a(InputState *s, uint16_t type, uint16_t code, int32_t value) {
    if (type == EV_KEY && code == BTN_TOUCH) {
        if (value != 0 && value != 1) return 0;
        s->a_touch_down = value;
    } else if (type == EV_ABS) {
        if (code == ABS_MT_SLOT) return 0; /* not a slot protocol */
        if (code == ABS_MT_TRACKING_ID) {
            if (value < -1 || (s->a_fields & 1U)) return 0;
            s->a_packet.tracking_id = value; s->a_fields |= 1U;
        } else if (code == ABS_MT_POSITION_X) {
            if (s->a_fields & 2U) return 0;
            s->a_packet.raw_x = value; s->a_fields |= 2U;
        } else if (code == ABS_MT_POSITION_Y) {
            if (s->a_fields & 4U) return 0;
            s->a_packet.raw_y = value; s->a_fields |= 4U;
        }
    } else if (type == EV_SYN && code == SYN_MT_REPORT) {
        return finish_a_packet(s);
    } else if (type == EV_SYN && code == SYN_REPORT) {
        return commit_a(s) ? 2 : 0;
    }
    return 1;
}

static int commit(InputState *s) {
    return s->protocol == INPUT_PROTOCOL_MT_B ? commit_mt(s) : commit_single(s);
}

static void dropped(InputState *s) {
    cancel_published(s);
    s->drop_pending = 1;
    s->suppress_until_all_up = 1;
    frame_begin(s, 1);
    s->committed.suppressed = 1;
}

int input_state_feed(InputState *s, uint16_t type, uint16_t code, int32_t value) {
    if (!s || !s->transform_valid || s->disconnected) return 0;

    if (type == EV_SYN && code == SYN_DROPPED) {
        dropped(s);
        return 2;
    }
    if (s->drop_pending) {
        if (type == EV_SYN && code == SYN_REPORT) return 3;
        return 1;
    }

    if (s->protocol == INPUT_PROTOCOL_MT_A) return feed_a(s, type, code, value);

    if (s->protocol == INPUT_PROTOCOL_MT_B) {
        if (type == EV_ABS) {
            if (code == ABS_MT_SLOT) {
                if (value < 0 || (unsigned)value >= s->slot_count) return 0;
                s->current_slot = value;
            } else if (code == ABS_MT_TRACKING_ID) {
                InputSlot *slot = &s->slots[s->current_slot];
                if (value < -1) return 0;
                if (value == -1) {
                    slot->active = 0;
                    slot->tracking_id = -1;
                    /* Protocol B axes are stateful: an unchanged coordinate
                       need not be sent again on the next tracking id. */
                    /* Keep published until SYN_REPORT: the guest still sees
                       the previous contact until the report commits. */
                } else {
                    if (slot->published &&
                        (!slot->active || slot->tracking_id != value)) {
                        queue_cancel(s, s->current_slot);
                        slot->defer_publish = 1;
                    }
                    slot->active = 1;
                    slot->tracking_id = value;
                }
            } else if (code == ABS_MT_POSITION_X) {
                InputSlot *slot = &s->slots[s->current_slot];
                slot->raw_x = value;
                slot->have_x = 1;
            } else if (code == ABS_MT_POSITION_Y) {
                InputSlot *slot = &s->slots[s->current_slot];
                slot->raw_y = value;
                slot->have_y = 1;
            }
        }
    } else {
        if (type == EV_ABS && code == ABS_X) {
            s->legacy_raw_x = value;
            s->legacy_have_x = 1;
        } else if (type == EV_ABS && code == ABS_Y) {
            s->legacy_raw_y = value;
            s->legacy_have_y = 1;
        } else if (type == EV_KEY && code == BTN_TOUCH) {
            s->legacy_down = value != 0;
        }
    }

    if (type == EV_SYN && code == SYN_REPORT) {
        if (!commit(s)) return 0;
        return 2;
    }
    return 1;
}

int input_state_resync_mt(InputState *s, const InputMtSnapshot *snapshot) {
    if (!s || s->protocol != INPUT_PROTOCOL_MT_B || !snapshot ||
        snapshot->slot_count != s->slot_count ||
        snapshot->slot_count > INPUT_HW_MAX_SLOTS ||
        snapshot->current_slot < 0 ||
        (unsigned)snapshot->current_slot >= s->slot_count)
        return 0;
    for (unsigned i = 0; i < snapshot->slot_count; ++i)
        if (snapshot->tracking_id[i] < -1 || snapshot->have_position[i] > 1)
            return 0;

    for (unsigned i = 0; i < s->slot_count; ++i) {
        InputSlot *slot = &s->slots[i];
        memset(slot, 0, sizeof(*slot));
        slot->tracking_id = snapshot->tracking_id[i];
        slot->active = snapshot->tracking_id[i] >= 0;
        if (snapshot->have_position[i]) {
            slot->raw_x = snapshot->raw_x[i];
            slot->raw_y = snapshot->raw_y[i];
            slot->have_x = 1;
            slot->have_y = 1;
        }
    }
    s->drop_pending = 0;
    s->suppress_until_all_up = active_count(s) != 0;
    s->overflowed = active_count(s) > INPUT_RUNTIME_MAX_CONTACTS;
    s->current_slot = snapshot->current_slot;
    return 1;
}

int input_state_resync_a(InputState *s, int touching) {
    if (!s || s->protocol != INPUT_PROTOCOL_MT_A ||
        (touching != 0 && touching != 1)) return 0;
    cancel_published(s);
    clear_slots(s);
    s->drop_pending = 0;
    s->a_touch_down = touching;
    s->suppress_until_all_up = touching;
    s->overflowed = 0;
    return 1;
}

void input_state_disconnect(InputState *s) {
    if (!s || s->disconnected) return;
    cancel_published(s);
    clear_slots(s);
    s->suppress_until_all_up = 1;
    s->disconnected = 1;
    frame_begin(s, 0);
    s->committed.suppressed = 1;
}

const InputFrame *input_state_frame(const InputState *s) {
    return s ? &s->committed : NULL;
}
