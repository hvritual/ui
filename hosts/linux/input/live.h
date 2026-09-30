#ifndef COFFEE_LINUX_INPUT_LIVE_H
#define COFFEE_LINUX_INPUT_LIVE_H

#include "state.h"
#include <stdint.h>
#include <stdio.h>

#define INPUT_LIVE_READ_BUDGET 8U

typedef struct {
    /* "auto" admits only known ilitek_ts/goodix-ts devices, uniquely. */
    const char *expected_name;
    unsigned width;
    unsigned height;
    int swap_xy;
    int invert_x;
    int invert_y;
    /* 0/0 means probe independent X/Y ranges; nonzero pair is an assertion. */
    int expected_raw_min;
    int expected_raw_max;
    unsigned expected_slots; /* 0 = probe B slots / no hardware slots for A */
} InputLiveConfig;

/* Numeric capability metadata only: never input text or individual events. */
typedef struct {
    int name_queried, name_matched, capabilities_queried, axes_queried;
    InputProtocol protocol;
    int tracking_min, tracking_max;
    int ev_key, ev_abs, btn_touch, mt_slot, mt_tracking, mt_x, mt_y;
    int slot_min, slot_max, raw_x_min, raw_x_max, raw_y_min, raw_y_max;
    unsigned scanned, opened_candidates, rejected_candidates;
} InputLiveDiagnostics;

typedef struct {
    int fd;
    int opened;
    int kernel_monotonic;
    char path[64];
    char name[256];
    InputState state;
    InputLiveConfig config;
    InputLiveDiagnostics diagnostics;
    uint64_t frames;
    uint64_t events;
    uint64_t syn_dropped;
    uint64_t resyncs;
    uint64_t disconnects;
    uint64_t last_event_ns;
    uint64_t reconnect_attempts, reconnects, budget_yields;
    int cleanup_errno;
    const char *error;
    int system_errno;
} InputLive;

typedef int (*InputFrameSink)(void *context, const InputFrame *frame, uint64_t event_ns);

/* Production device selection: scan event nodes, match known name + protocol-specific capabilities.
   The observed event number is not identity. */
int input_live_discover(InputLive *live, const char *input_dir, const InputLiveConfig *config);

/* Explicit-path entry is for evidence/tests. It still validates name/capabilities/ranges. */
int input_live_open_path(InputLive *live, const char *path, const InputLiveConfig *config);

/* 1 readable, 0 timeout, -1 error/disconnect. timeout_ms is 0..1000. */
int input_live_wait(InputLive *live, int timeout_ms);

/* Drain up to INPUT_LIVE_READ_BUDGET batches of 64 events. Emits each committed frame synchronously.
   Returns frame count, 0 when idle, -1 on fatal read/resync error. */
int input_live_drain(InputLive *live, InputFrameSink sink, void *context);

/* Caller schedules retries (e.g. every 500ms). Returns 1 after re-admission,
   0 otherwise, and never sleeps or restarts unrelated services. */
int input_live_reconnect(InputLive *live, const char *input_dir);
void input_live_close(InputLive *live);

/* One bounded JSON record with expected/observed admission values and errno.
 * Returns zero on output error. No second probe or relaxed admission path. */
int input_live_report(FILE *out, const InputLive *live);

#endif
