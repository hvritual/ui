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

static int resync_mt(InputLive *live);
static int probe_axes(const InputLiveConfig *c) {
    return c->expected_raw_min == 0 && c->expected_raw_max == 0;
}
static int known_touch(const char *name) {
    return !strcmp(name, "ilitek_ts") || !strcmp(name, "goodix-ts");
}

static int validate_candidate(InputLive *live, const InputLiveConfig *config) {
    unsigned long ev[NBITS(EV_MAX + 1)];
    unsigned long key[NBITS(KEY_MAX + 1)];
    unsigned long abs[NBITS(ABS_MAX + 1)];
    struct input_absinfo slot = {0}, tracking, pos_x, pos_y;
    char name[256] = {0};

    if (ioctl(live->fd, EVIOCGNAME(sizeof(name)), name) < 0)
        return fail(live, "INPUT_NAME_QUERY_FAILED", errno);
    name[sizeof(name) - 1] = 0;
    live->diagnostics.name_queried = 1;
    if (!copy_text(live->name, sizeof(live->name), name))
        return fail(live, "INPUT_NAME_TOO_LONG", 0);
    live->diagnostics.name_matched = !strcmp(config->expected_name, "auto") ?
        known_touch(name) : !strcmp(name, config->expected_name);
    if (!live->diagnostics.name_matched)
        return fail(live, "INPUT_NAME_MISMATCH", 0);

    if (!query_bits(live->fd, 0, ev, sizeof(ev)))
        return fail(live, "INPUT_EV_QUERY_FAILED", errno);
    live->diagnostics.ev_key = (int)TEST_BIT(EV_KEY, ev);
    live->diagnostics.ev_abs = (int)TEST_BIT(EV_ABS, ev);
    if (!live->diagnostics.ev_key || !live->diagnostics.ev_abs)
        return fail(live, "INPUT_REQUIRED_EV_MISSING", 0);
    if (!query_bits(live->fd, EV_KEY, key, sizeof(key)) ||
        !query_bits(live->fd, EV_ABS, abs, sizeof(abs)))
        return fail(live, "INPUT_CAPABILITY_QUERY_FAILED", errno);

    live->diagnostics.capabilities_queried = 1;
    live->diagnostics.btn_touch = (int)TEST_BIT(BTN_TOUCH, key);
    live->diagnostics.mt_slot = (int)TEST_BIT(ABS_MT_SLOT, abs);
    live->diagnostics.mt_tracking = (int)TEST_BIT(ABS_MT_TRACKING_ID, abs);
    live->diagnostics.mt_x = (int)TEST_BIT(ABS_MT_POSITION_X, abs);
    live->diagnostics.mt_y = (int)TEST_BIT(ABS_MT_POSITION_Y, abs);

    if (!live->diagnostics.btn_touch || !live->diagnostics.mt_tracking ||
        !live->diagnostics.mt_x || !live->diagnostics.mt_y)
        return fail(live, "INPUT_MT_CAPABILITIES_MISSING", 0);

    /* The observed Goodix BSP has tracking IDs but no ABS_MT_SLOT. It needs
     * packet-based MT-A, not a relaxed B check followed by B-only resync. */
    InputProtocol protocol = live->diagnostics.mt_slot ? INPUT_PROTOCOL_MT_B :
        !strcmp(name, "goodix-ts") ? INPUT_PROTOCOL_MT_A : 0;
    live->diagnostics.protocol = protocol;
    if (!query_abs(live->fd, ABS_MT_TRACKING_ID, &tracking) ||
        !query_abs(live->fd, ABS_MT_POSITION_X, &pos_x) ||
        !query_abs(live->fd, ABS_MT_POSITION_Y, &pos_y) ||
        (live->diagnostics.mt_slot && !query_abs(live->fd, ABS_MT_SLOT, &slot)))
        return fail(live, "INPUT_AXIS_QUERY_FAILED", errno);

    live->diagnostics.axes_queried = 1;
    live->diagnostics.slot_min = live->diagnostics.mt_slot ? slot.minimum : -1;
    live->diagnostics.slot_max = live->diagnostics.mt_slot ? slot.maximum : -1;
    live->diagnostics.raw_x_min = pos_x.minimum;
    live->diagnostics.raw_x_max = pos_x.maximum;
    live->diagnostics.raw_y_min = pos_y.minimum;
    live->diagnostics.raw_y_max = pos_y.maximum;
    live->diagnostics.tracking_min = tracking.minimum;
    live->diagnostics.tracking_max = tracking.maximum;
    if (!protocol) return fail(live, "INPUT_PROTOCOL_B_MISSING", 0);
    if (pos_x.maximum <= pos_x.minimum || pos_y.maximum <= pos_y.minimum)
        return fail(live, "INPUT_AXIS_RANGE_INVALID", 0);
    if (tracking.minimum < 0 || tracking.maximum < tracking.minimum)
        return fail(live, "INPUT_TRACKING_RANGE_INVALID", 0);
    if (!probe_axes(config) &&
        (pos_x.minimum != config->expected_raw_min || pos_x.maximum != config->expected_raw_max ||
         pos_y.minimum != config->expected_raw_min || pos_y.maximum != config->expected_raw_max))
        return fail(live, "INPUT_PROFILE_MISMATCH", 0);

    unsigned capacity = INPUT_HW_MAX_SLOTS; /* bounded A packet capacity, not HW slots */
    if (protocol == INPUT_PROTOCOL_MT_B) {
        int64_t slots = (int64_t)slot.maximum - slot.minimum + 1;
        if (slot.minimum != 0 || slots <= 0 || slots > INPUT_HW_MAX_SLOTS)
            return fail(live, "INPUT_SLOT_RANGE_INVALID", 0);
        capacity = (unsigned)slots;
        if (config->expected_slots && config->expected_slots != capacity)
            return fail(live, "INPUT_PROFILE_MISMATCH", 0);
    } else if (config->expected_slots) {
        return fail(live, "INPUT_SLOT_PROFILE_INAPPLICABLE", 0);
    }

    InputTransform transform = {
        .x = {.minimum = pos_x.minimum, .maximum = pos_x.maximum},
        .y = {.minimum = pos_y.minimum, .maximum = pos_y.maximum},
        .width = config->width, .height = config->height,
        .swap_xy = config->swap_xy, .invert_x = config->invert_x, .invert_y = config->invert_y,
    };
    if (!input_state_init(&live->state, protocol, capacity, &transform))
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
        if (close(fd)) live->cleanup_errno = errno; /* Never retry close(EINTR). */
    }
}

static int config_valid(const InputLiveConfig *config) {
    return config && config->expected_name && config->expected_name[0] &&
           config->width && config->height && config->width <= 1024 && config->height <= 1024 &&
           config->expected_slots <= INPUT_HW_MAX_SLOTS &&
           (probe_axes(config) || config->expected_raw_max > config->expected_raw_min);
}

int input_live_open_path(InputLive *live, const char *path, const InputLiveConfig *config) {
    struct stat st;
    if (!live || !path || !config_valid(config))
        return fail(live, "INPUT_CONFIG_INVALID", EINVAL);

    /* The caller may pass &live->config when re-opening this object. */
    InputLiveConfig saved_config = *config;
    memset(live, 0, sizeof(*live));
    live->fd = -1;
    live->config = saved_config;
    config = &live->config;
    if (!copy_text(live->path, sizeof(live->path), path))
        return fail(live, "INPUT_PATH_TOO_LONG", ENAMETOOLONG);

    live->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (live->fd < 0) return fail(live, "INPUT_OPEN_FAILED", errno);
    live->opened = 1;

    if (fstat(live->fd, &st))
        goto stat_failed;
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 13) {
        fail(live, "INPUT_NOT_EVDEV", 0);
        goto rejected;
    }
    if (!validate_candidate(live, config) || !resync_mt(live))
        goto rejected;
    return 1;

stat_failed:
    fail(live, "INPUT_STAT_FAILED", errno);
rejected:
    input_live_close(live);
    return 0;
}

static void json_text(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; p && *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 32 || *p >= 127) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

int input_live_report(FILE *out, const InputLive *live) {
    if (!out || !live) return 0;
    const InputLiveConfig *c = &live->config;
    const InputLiveDiagnostics *d = &live->diagnostics;
    fputs("{\"schema\":1,\"operation\":\"live-input-admission\",\"path\":", out);
    json_text(out, live->path);
    fputs(",\"name\":", out); json_text(out, live->name);
    fputs(",\"protocol\":", out);
    json_text(out, d->protocol == INPUT_PROTOCOL_MT_A ? "mt-a" :
                   d->protocol == INPUT_PROTOCOL_MT_B ? "mt-b" : "unselected");
    fprintf(out, ",\"axis_source\":\"%s\",\"slot_range_present\":%s,"
                 "\"contact_capacity\":%u,\"tracking_min\":%d,\"tracking_max\":%d,"
                 "\"orientation_verified\":false",
            probe_axes(c) ? "kernel-probe" : "explicit-constraints",
            d->mt_slot && d->axes_queried ? "true" : "false",
            live->state.slot_count, d->tracking_min, d->tracking_max);
    fputs(",\"error\":", out);
    if (live->error) json_text(out, live->error); else fputs("null", out);
    fprintf(out, ",\"errno\":%d,\"cleanup_errno\":%d,\"admitted\":%s,"
                 "\"name_queried\":%s,\"name_matched\":%s,\"expected\":{\"name\":",
            live->system_errno, live->cleanup_errno, live->opened && !live->error ? "true" : "false",
            d->name_queried ? "true" : "false", d->name_matched ? "true" : "false");
    json_text(out, c->expected_name);
    fprintf(out, ",\"width\":%u,\"height\":%u,\"raw_min\":%d,\"raw_max\":%d,"
                 "\"slots\":%u,\"swap_xy\":%d,\"invert_x\":%d,\"invert_y\":%d},"
                 "\"capabilities_queried\":%s,\"capabilities\":{\"ev_key\":%d,\"ev_abs\":%d,"
                 "\"btn_touch\":%d,\"mt_slot\":%d,\"mt_tracking\":%d,\"mt_x\":%d,\"mt_y\":%d},"
                 "\"axes_queried\":%s,\"axes\":{\"slot_min\":%d,\"slot_max\":%d,"
                 "\"raw_x_min\":%d,\"raw_x_max\":%d,\"raw_y_min\":%d,\"raw_y_max\":%d},"
                 "\"scanned\":%u,\"opened_candidates\":%u,\"rejected_candidates\":%u}\n",
            c->width, c->height, c->expected_raw_min, c->expected_raw_max, c->expected_slots,
            c->swap_xy, c->invert_x, c->invert_y, d->capabilities_queried ? "true" : "false",
            d->ev_key, d->ev_abs, d->btn_touch, d->mt_slot, d->mt_tracking, d->mt_x, d->mt_y,
            d->axes_queried ? "true" : "false", d->slot_min, d->slot_max,
            d->raw_x_min, d->raw_x_max, d->raw_y_min, d->raw_y_max,
            d->scanned, d->opened_candidates, d->rejected_candidates);
    return !ferror(out);
}

int input_live_discover(InputLive *live, const char *input_dir, const InputLiveConfig *config) {
    char path[64];
    if (!live || !input_dir || !config_valid(config))
        return fail(live, "INPUT_CONFIG_INVALID", EINVAL);
    InputLiveConfig saved_config = *config;
    config = &saved_config;
    InputLive best = {0};
    best.fd = -1; best.config = *config;
    InputLive selected = {0}; selected.fd = -1;
    unsigned opened = 0, rejected = 0;
    int best_rank = 0;

    for (unsigned i = 0; i < 64; ++i) {
        int n = snprintf(path, sizeof(path), "%s/event%u", input_dir, i);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            input_live_close(&selected);
            *live = best;
            return fail(live, "INPUT_PATH_TOO_LONG", ENAMETOOLONG);
        }
        InputLive candidate = {0};
        int accepted = input_live_open_path(&candidate, path, config);
        int open_failed = candidate.error && !strcmp(candidate.error, "INPUT_OPEN_FAILED");
        if (!open_failed) ++opened;
        /* Do not print 64 ENOENT lines on every scan. Existing or inaccessible
         * candidates keep their exact errno, query result and expected values. */
        if (!accepted && open_failed && candidate.system_errno == ENOENT) continue;
        if (!accepted) ++rejected;
        candidate.diagnostics.scanned = i + 1;
        candidate.diagnostics.opened_candidates = opened;
        candidate.diagnostics.rejected_candidates = rejected;
        fputs("INPUT_CANDIDATE ", stderr);
        (void)input_live_report(stderr, &candidate);
        if (candidate.cleanup_errno) {
            input_live_close(&selected);
            *live = candidate;
            return fail(live, "INPUT_CLOSE_FAILED", candidate.cleanup_errno);
        }
        if (accepted) {
            if (strcmp(config->expected_name, "auto")) { *live = candidate; return 1; }
            if (selected.opened) {
                input_live_close(&candidate); input_live_close(&selected);
                *live = selected;
                return fail(live, "INPUT_DEVICE_AMBIGUOUS", 0);
            }
            selected = candidate;
            continue;
        }

        int permission = candidate.system_errno == EACCES || candidate.system_errno == EPERM;
        int rank = candidate.diagnostics.name_matched ? 4 : permission ? 3 :
                   candidate.diagnostics.name_queried ? 1 : 2;
        /* A later unrelated power key or missing event node must not erase the
         * failure on the expected touchscreen. Preserve the first highest rank. */
        if (rank > best_rank) {
            best = candidate; best_rank = rank;
            if (permission && !candidate.diagnostics.name_matched)
                best.error = "INPUT_DEVICE_PERMISSION";
        }
    }
    if (selected.opened) {
        selected.diagnostics.scanned = 64;
        selected.diagnostics.opened_candidates = opened;
        selected.diagnostics.rejected_candidates = rejected;
        *live = selected; return 1;
    }
    best.diagnostics.scanned = 64;
    best.diagnostics.opened_candidates = opened;
    best.diagnostics.rejected_candidates = rejected;
    *live = best;
    if (!best_rank) return fail(live, "INPUT_DEVICE_NOT_FOUND", ENOENT);
    return 0;
}

int input_live_wait(InputLive *live, int timeout_ms) {
    struct pollfd pfd;
    int rc;
    if (!live || !live->opened || live->fd < 0 || timeout_ms < 0 || timeout_ms > 1000)
        return -1;

    pfd.fd = live->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0 && errno == EINTR) return 0; /* Return to signal/deadline owner. */

    if (rc < 0) {
        fail(live, "INPUT_POLL_FAILED", errno);
        return -1;
    }
    if (rc == 0) return 0;
    if (pfd.revents & POLLNVAL) {
        fail(live, "INPUT_POLL_INVALID", 0);
        return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP)) {
        if (!live->error) fail(live, "INPUT_DEVICE_DISCONNECTED", ENODEV);
        return -1;
    }
    if (pfd.revents & POLLIN) return 1;
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
    if (live->state.protocol == INPUT_PROTOCOL_MT_A) {
        unsigned long keys[NBITS(KEY_MAX + 1)] = {0};
        if (ioctl(live->fd, EVIOCGKEY(sizeof(keys)), keys) < 0)
            return fail(live, "INPUT_KEY_STATE_QUERY_FAILED", errno);
        if (!input_state_resync_a(&live->state, (int)TEST_BIT(BTN_TOUCH, keys)))
            return fail(live, "INPUT_MT_A_RESYNC_REJECTED", 0);
        ++live->resyncs;
        return 1;
    }
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
        snapshot.have_position[i] = 1U; /* Includes inactive stateful axes. */

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
    if (!live->error) fail(live, "INPUT_DEVICE_DISCONNECTED", ENODEV);
    return 0;
}

int input_live_drain(InputLive *live, InputFrameSink sink, void *context) {
    int emitted = 0;
    if (!live || !live->opened || live->fd < 0) return -1;

    for (unsigned batch = 0; batch < INPUT_LIVE_READ_BUDGET; ++batch) {
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
    ++live->budget_yields;
    return emitted;
}

int input_live_reconnect(InputLive *live, const char *input_dir) {
    if (!live || live->opened || !input_dir || !live->config.expected_name) return 0;
    ++live->reconnect_attempts;
    InputLive next = {0};
    if (!input_live_discover(&next, input_dir, &live->config)) return 0;
    if (live->name[0] &&
        (strcmp(next.name, live->name) || next.state.protocol != live->state.protocol ||
         next.state.slot_count != live->state.slot_count ||
         next.state.transform.x.minimum != live->state.transform.x.minimum ||
         next.state.transform.x.maximum != live->state.transform.x.maximum ||
         next.state.transform.y.minimum != live->state.transform.y.minimum ||
         next.state.transform.y.maximum != live->state.transform.y.maximum)) {
        input_live_close(&next);
        return fail(live, "INPUT_RECONNECT_PROFILE_CHANGED", 0);
    }
    next.frames += live->frames;
    next.events += live->events;
    next.syn_dropped += live->syn_dropped;
    next.resyncs += live->resyncs;
    next.disconnects = live->disconnects;
    next.reconnects = live->reconnects + 1;
    next.reconnect_attempts = live->reconnect_attempts;
    next.budget_yields = live->budget_yields;
    next.last_event_ns = live->last_event_ns;
    next.cleanup_errno = live->cleanup_errno;
    *live = next;
    return 1;
}
