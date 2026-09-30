#ifndef COFFEE_LINUX_INPUT_STATE_H
#define COFFEE_LINUX_INPUT_STATE_H
#include <linux/input.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_HW_MAX_SLOTS 32
#define INPUT_RUNTIME_MAX_CONTACTS 8

typedef enum {
    INPUT_PROTOCOL_SINGLE = 1,
    INPUT_PROTOCOL_MT_B = 2,
    INPUT_PROTOCOL_MT_A = 3
} InputProtocol;

typedef struct {
    int minimum;
    int maximum;
} InputAxisRange;

typedef struct {
    InputAxisRange x;
    InputAxisRange y;
    unsigned width;
    unsigned height;
    int swap_xy;
    int invert_x;
    int invert_y;
} InputTransform;

typedef struct {
    int id;
    int x;
    int y;
} InputContact;

typedef struct {
    unsigned contact_count;
    InputContact contacts[INPUT_RUNTIME_MAX_CONTACTS];
    unsigned cancelled_count;
    int cancelled[INPUT_RUNTIME_MAX_CONTACTS];
    uint64_t sequence;
    int suppressed;
    int syn_dropped;
} InputFrame;

typedef struct {
    int active;
    int tracking_id;
    int raw_x;
    int raw_y;
    int have_x;
    int have_y;
    int published;
    int defer_publish;
} InputSlot;

typedef struct {
    InputProtocol protocol;
    unsigned slot_count;
    int current_slot;
    InputSlot slots[INPUT_HW_MAX_SLOTS];

    /* Type A assembles complete packets; slots retain logical IDs across reorder. */
    InputSlot a_packet;
    InputSlot a_contacts[INPUT_HW_MAX_SLOTS];
    unsigned a_fields, a_count;
    int a_touch_down;

    int legacy_raw_x;
    int legacy_raw_y;
    int legacy_have_x;
    int legacy_have_y;
    int legacy_down;

    InputTransform transform;
    int transform_valid;

    int drop_pending;
    int suppress_until_all_up;
    int disconnected;
    int overflowed;

    int cancel_queue[INPUT_RUNTIME_MAX_CONTACTS];
    unsigned cancel_count;

    InputFrame committed;
    uint64_t sequence;
} InputState;

typedef struct {
    unsigned slot_count;
    int current_slot;
    int tracking_id[INPUT_HW_MAX_SLOTS];
    int raw_x[INPUT_HW_MAX_SLOTS];
    int raw_y[INPUT_HW_MAX_SLOTS];
    unsigned have_position[INPUT_HW_MAX_SLOTS];
} InputMtSnapshot;

int input_map_point(const InputTransform *transform, int raw_x, int raw_y, int *x, int *y);
int input_state_init(InputState *state, InputProtocol protocol, unsigned slot_count,
                     const InputTransform *transform);
/* feed: 0 invalid/error, 1 accepted-no-frame, 2 committed-frame, 3 caller-must-resync-after-SYN_DROPPED. */
int input_state_feed(InputState *state, uint16_t type, uint16_t code, int32_t value);
int input_state_resync_mt(InputState *state, const InputMtSnapshot *snapshot);
/* Type A has no slot snapshot ioctl. Re-arm after querying BTN_TOUCH. */
int input_state_resync_a(InputState *state, int touching);
void input_state_disconnect(InputState *state);
const InputFrame *input_state_frame(const InputState *state);

#endif
