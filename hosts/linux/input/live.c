#define _POSIX_C_SOURCE 200809L
#include "live.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define NBITS(x) ((((x) - 1U) / BITS_PER_LONG) + 1U)
#define TEST_BIT(bit, array) (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1UL)

static int fail(InputLive *live, const char *code, int number) {
    if (live) {
        live->error = code;
        live->system_errno = number;
    }
    return 0;
}

static int query_bits(int fd, unsigned type, unsigned long *bits, size_t bytes) {
    memset(bits, 0, bytes);
    return ioctl(fd, EVIOCGBIT(type, bytes), bits) >= 0;
}

static int query_abs(int fd, unsigned code, struct input_absinfo *out) {
    memset(out, 0, sizeof(*out));
    return ioctl(fd, EVIOCGABS(code), out) == 0;
}

static uint64_t monotonic_ns(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t event_ns(const InputLive *live, const struct input_event *event) {
    if (live->kernel_monotonic) {
        return (uint64_t)event->time.tv_sec * 1000000000ULL +
               (uint64_t)event->time.tv_usec * 1000ULL;
    }
    return monotonic_ns();
}

static int copy_text(char *dst, size_t size, const char *src) {
    size_t length;
    if (!dst || size == 0 || !src) return 0;
    length = strlen(src);
    if (length >= size) return 0;
    memcpy(dst, src, length + 1);
    return 1;
}

static int validate_candidate(InputLive *live, const InputLiveConfig *config) {
    unsigned long ev[NBITS(EV_MAX + 1)];
    unsigned long key[NBITS(KEY_MAX + 1)];
    unsigned long abs[NBITS(ABS_MAX + 1)];
    struct input_absinfo slot, tracking, pos_x, pos_y;
    char name[256] = {0};

    if (ioctl(live->fd, EVIOCGNAME(sizeof(name)), name) < 0)
        return fail(live, "INPUT_NAME_QUERY_FAILED", errno);
    if (strcmp(name, config->expected_name))
        return fail(live, "INPUT_NAME_MISMATCH", 0);

    if (!query_bits(live->fd, 0, ev, sizeof(ev)))
        return fail(live, "INPUT_EV_QUERY_FAILED", errno);
    if (!TEST_BIT(EV_KEY, ev) || !TEST_BIT(EV_ABS, ev))
        return fail(live, "INPUT_REQUIRED_EV_MISSING", 0);
    if (!query_bits(live->fd, EV_KEY, key, sizeof(key)) ||
        !query_bits(live->fd, EV_ABS, abs, sizeof(abs)))
        return fail(live, "INPUT_CAPABILITY_QUERY_FAILED", errno);

    if (!TEST_BIT(BTN_TOUCH, key) ||
        !TEST_BIT(ABS_MT_SLOT, abs) ||
        !TEST_BIT(ABS_MT_TRACKING_ID, abs) ||
        !TEST_BIT(ABS_MT_POSITION_X, abs) ||
        !TEST_BIT(ABS_MT_POSITION_Y, abs))
        return fail(live, "INPUT_PROTOCOL_B_MISSING", 0);

    if (!query_abs(live->fd, ABS_MT_SLOT, &slot) ||
        !query_abs(live->fd, ABS_MT_TRACKING_ID, &tracking) ||
        !query_abs(live->fd, ABS_MT_POSITION_X, &pos_x) ||
        !query_abs(live->fd, ABS_MT_POSITION_Y, &pos_y))
        return fail(live, "INPUT_AXIS_QUERY_FAILED", errno);

    if (slot.minimum != 0 || slot.maximum < slot.minimum ||
        (unsigned)(slot.maximum - slot.minimum + 1) != config->expected_slots ||
        pos_x.minimum != config->expected_raw_min ||
        pos_x.maximum != config->expected_raw_max ||
        pos_y.minimum != config->expected_raw_min ||
        pos_y.maximum != config->expected_raw_max)
        return fail(live, "INPUT_PROFILE_MISMATCH", 0);

    if (tracking.maximum < tracking.minimum)
        return fail(live, "INPUT_TRACKING_RANGE_INVALID", 0);

    if (!copy_text(live->name, sizeof(live->name), name))
        return fail(live, "INPUT_NAME_TOO_LONG", 0);

    InputTransform transform = {
        .x = {.minimum = pos_x.minimum, .maximum = pos_x.maximum},
        .y = {.minimum = pos_y.minimum, .maximum = pos_y.maximum},
        .width = config->width,
        .height = config->height,
        .swap_xy = config->swap_xy,
        .invert_x = config->invert_x,
        .invert_y = config->invert_y,
    };
    if (!input_state_init(&live->state, INPUT_PROTOCOL_MT_B,
                          config->expected_slots, &transform))
        return fail(live, "INPUT_STATE_INIT_FAILED", 0);

#ifdef EVIOCSCLOCKID
    {
        int clock_id = CLOCK_MONOTONIC;
        if (ioctl(live->fd, EVIOCSCLOCKID, &clock_id) == 0)
            live->kernel_monotonic = 1;
    }
#endif
    return 1;
}

void input_live_close(InputLive *live) {
    if (!live) return;
    if (live->opened) {
        int fd = live->fd;
        live->opened = 0;
        live->fd = -1;
        (void)close(fd);
    }
}

int input_live_open_path(InputLive *live, const char *path, const InputLiveConfig *config) {
    struct stat st;
    if (!live || !path || !config || !config->expected_name ||
        !config->expected_name[0] || config->width == 0 || config->height == 0 ||
        config->expected_slots == 0 ||
        config->expected_slots > INPUT_HW_MAX_SLOTS ||
        config->expected_raw_max <= config->expected_raw_min)
        return 0;

    memset(live, 0, sizeof(*live));
    live->fd = -1;
    live->config = *config;

    live->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (live->fd < 0) return fail(live, "INPUT_OPEN_FAILED", errno);
    live->opened = 1;

    if (fstat(live->fd, &st))
        goto stat_failed;
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 13) {
        fail(live, "INPUT_NOT_EVDEV", 0);
        goto rejected;
    }
    if (!copy_text(live->path, sizeof(live->path), path)) {
        fail(live, "INPUT_PATH_TOO_LONG", 0);
        goto rejected;
    }
    if (!validate_candidate(live, config))
        goto rejected;
    return 1;

stat_failed:
    fail(live, "INPUT_STAT_FAILED", errno);
rejected:
    input_live_close(live);
    return 0;
}

int input_live_discover(InputLive *live, const char *input_dir, const InputLiveConfig *config) {
    char path[64];
    int last_errno = 0;
    if (!live || !input_dir || !config) return 0;

    for (unsigned i = 0; i < 64; ++i) {
        int n = snprintf(path, sizeof(path), "%s/event%u", input_dir, i);
        if (n < 0 || (size_t)n >= sizeof(path)) return 0;

        InputLive candidate = {0};
        if (input_live_open_path(&candidate, path, config)) {
            *live = candidate;
            return 1;
        }
        if (candidate.system_errno == EACCES || candidate.system_errno == EPERM)
            last_errno = candidate.system_errno;
    }
    memset(live, 0, sizeof(*live));
    live->fd = -1;
    return fail(live, last_errno ? "INPUT_DEVICE_PERMISSION" : "INPUT_DEVICE_NOT_FOUND",
                last_errno);
}

int input_live_wait(InputLive *live, int timeout_ms) {
    struct pollfd pfd;
    int rc;
    if (!live || !live->opened || live->fd < 0 || timeout_ms < 0 || timeout_ms > 1000)
        return -1;

    pfd.fd = live->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        fail(live, "INPUT_POLL_FAILED", errno);
        return -1;
    }
    if (rc == 0) return 0;
    if (pfd.revents & POLLNVAL) {
        fail(live, "INPUT_POLL_INVALID", 0);
        return -1;
    }
    if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) return 1;
    return 0;
}

static int mt_slots(int fd, unsigned slot_count, unsigned code, int *values) {
    int32_t buffer[INPUT_HW_MAX_SLOTS + 1];
    size_t bytes;
    if (!values || slot_count == 0 || slot_count > INPUT_HW_MAX_SLOTS) return 0;
    bytes = (slot_count + 1U) * sizeof(buffer[0]);
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = (int32_t)code;
    if (ioctl(fd, EVIOCGMTSLOTS(bytes), buffer) < 0) return 0;
    for (unsigned i = 0; i < slot_count; ++i)
        values[i] = buffer[i + 1U];
    return 1;
}

static int resync_mt(InputLive *live) {
    InputMtSnapshot snapshot;
    struct input_absinfo current;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.slot_count = live->state.slot_count;

    if (!mt_slots(live->fd, snapshot.slot_count, ABS_MT_TRACKING_ID,
                  snapshot.tracking_id) ||
        !mt_slots(live->fd, snapshot.slot_count, ABS_MT_POSITION_X,
                  snapshot.raw_x) ||
        !mt_slots(live->fd, snapshot.slot_count, ABS_MT_POSITION_Y,
                  snapshot.raw_y) ||
        !query_abs(live->fd, ABS_MT_SLOT, &current))
        return fail(live, "INPUT_MT_RESYNC_FAILED", errno);

    snapshot.current_slot = current.value;
    for (unsigned i = 0; i < snapshot.slot_count; ++i)
        snapshot.have_position[i] = snapshot.tracking_id[i] >= 0 ? 1U : 0U;

    if (!input_state_resync_mt(&live->state, &snapshot))
        return fail(live, "INPUT_MT_RESYNC_REJECTED", 0);

    ++live->resyncs;
    return 1;
}

static int emit_frame(InputLive *live, InputFrameSink sink, void *context,
                      uint64_t event_time) {
    const InputFrame *frame = input_state_frame(&live->state);
    if (!frame) return fail(live, "INPUT_FRAME_MISSING", 0);
    if (sink && !sink(context, frame, event_time))
        return fail(live, "INPUT_SINK_FAILED", 0);
    ++live->frames;
    return 1;
}

static int disconnect(InputLive *live, InputFrameSink sink, void *context) {
    if (!live->state.disconnected) {
        input_state_disconnect(&live->state);
        ++live->disconnects;
        if (!emit_frame(live, sink, context, live->last_event_ns))
            return 0;
    }
    fail(live, "INPUT_DEVICE_DISCONNECTED", ENODEV);
    return 0;
}

int input_live_drain(InputLive *live, InputFrameSink sink, void *context) {
    int emitted = 0;
    if (!live || !live->opened || live->fd < 0) return -1;

    for (;;) {
        struct input_event events[64];
        ssize_t bytes = read(live->fd, events, sizeof(events));

        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return emitted;
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes < 0) {
            fail(live, "INPUT_READ_FAILED", errno);
            (void)disconnect(live, sink, context);
            return -1;
        }
        if (bytes == 0) {
            (void)disconnect(live, sink, context);
            return -1;
        }
        if ((size_t)bytes % sizeof(events[0])) {
            fail(live, "INPUT_PARTIAL_EVENT", EPROTO);
            (void)disconnect(live, sink, context);
            return -1;
        }

        size_t count = (size_t)bytes / sizeof(events[0]);
        for (size_t i = 0; i < count; ++i) {
            struct input_event *event = &events[i];
            uint64_t when = event_ns(live, event);
            if (!when) when = live->last_event_ns;
            if (when < live->last_event_ns)
                when = live->last_event_ns;
            live->last_event_ns = when;
            ++live->events;

            int result = input_state_feed(&live->state,
                                          event->type,
                                          event->code,
                                          event->value);
            if (result == 0) {
                fail(live, "INPUT_STATE_REJECTED_EVENT", 0);
                (void)disconnect(live, sink, context);
                return -1;
            }
            if (event->type == EV_SYN && event->code == SYN_DROPPED)
                ++live->syn_dropped;

            if (result == 2) {
                if (!emit_frame(live, sink, context, when))
                    return -1;
                ++emitted;
            } else if (result == 3) {
                if (!resync_mt(live))
                    return -1;
            }
        }
    }
}
