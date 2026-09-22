#ifndef TEST_FAKE_INPUT_H
#define TEST_FAKE_INPUT_H
#include <linux/input.h>
#include <stddef.h>

#define FAKE_INPUT_MAX_EVENTS 256
typedef struct {
    struct input_event events[FAKE_INPUT_MAX_EVENTS];
    size_t event_count;
    size_t event_index;
    int current_slot;
    int tracking[10];
    int x[10];
    int y[10];
    int opens, closes, polls, reads, ioctls, forbidden;
    int selected_flags;
    int read_zero;
    int poll_hup;
} FakeInput;

extern FakeInput fake_input;
void fake_input_reset(void);
void fake_input_push(unsigned type, unsigned code, int value, long sec, long usec);

#endif
