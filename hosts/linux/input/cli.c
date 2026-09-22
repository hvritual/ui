#define _POSIX_C_SOURCE 200809L
#include "cli.h"
#include "bridge.h"
#include "live.h"
#include "../display/fbdev.h"
#include "../host.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop_touch(int number) { interrupted = number; }

typedef struct {
    FILE *trace;
    uint64_t rows, guest_begin_ns, guest_end_ns, present_begin_ns, present_end_ns;
    unsigned long action_sequence;
    unsigned target_mask;
} TouchEvidence;

static int unsigned_arg(const char *text, unsigned long *out) {
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    char *end = NULL; errno = 0; unsigned long n = strtoul(text, &end, 10);
    if (errno || !end || *end || !n || n > 3600) return 0;
    *out = n; return 1;
}
static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; p && *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32 || *p >= 127) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}
static int open_report(const char *path, FILE **out, int *created) {
    if (!path) { *out = stdout; *created = 0; return 1; }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w");
    if (!f) { int saved = errno; close(fd); unlink(path); errno = saved; return 0; }
    *out = f; *created = 1; return 1;
}
static int runtime_hit(void *context, float x, float y) {
    (void)context; return pocket_runtime_hit_test_bounds(x, y);
}
static int bridge_sink(void *context, const InputFrame *frame, uint64_t ns) {
    return input_bridge_ingest(context, frame, ns);
}

/* All input sampling and guest/presentation timestamps share CLOCK_MONOTONIC.
   Present end means CPU submission completed, NOT that the LCD scanned out. */
static int turn(LinuxHost *host, InputBridge *bridge, FbDevice *display,
                TouchEvidence *e, int force_present) {
    PocketRuntimeContactsInput guest = {0}; uint64_t event_ns = 0;
    HostFrame frame;
    if (!input_bridge_next(bridge, &guest, &event_ns) ||
        !host_monotonic_ns(&e->guest_begin_ns) || !host_turn_contacts(host, &guest) ||
        !host_monotonic_ns(&e->guest_end_ns)) return 0;
    e->present_begin_ns = e->present_end_ns = 0;
    if (force_present || host->turns % 2 == 0) {
        if (!host_render(host, &frame) || !host_monotonic_ns(&e->present_begin_ns) ||
            !fbdev_present(display, &frame) || !host_monotonic_ns(&e->present_end_ns)) return 0;
    }
    unsigned long sequence = pocket_runtime_action_sequence();
    if (sequence != e->action_sequence) {
        const char *name = pocket_runtime_action_name();
        int target = pocket_runtime_action_value();
        if (name && !strcmp(name, "touch-target") && target >= 0 && target < 5)
            e->target_mask |= 1U << (unsigned)target;
        e->action_sequence = sequence;
    }
    if (e->trace) {
        fprintf(e->trace, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%lu\n",
                (unsigned long long)host->turns, (unsigned long long)bridge->delivered_frames,
                (unsigned long long)event_ns, (unsigned long long)e->guest_begin_ns,
                (unsigned long long)e->guest_end_ns, (unsigned long long)e->present_begin_ns,
                (unsigned long long)e->present_end_ns, guest.contact_count, guest.cancelled_count,
                e->action_sequence);
        ++e->rows;
        if (ferror(e->trace)) return 0;
    }
    return 1;
}
static int cancel_guest(LinuxHost *host, InputBridge *bridge, FbDevice *display,
                        TouchEvidence *e) {
    uint64_t now;
    if (host->state != HOST_RUNNING || !bridge->hit_test) return 1;
    if (!host_monotonic_ns(&now) || !input_bridge_cancel_all(bridge, now)) return 0;
    return turn(host, bridge, display, e, 1);
}
static int report_json(FILE *out, const char *fbpath, const InputLive *in,
                       const InputBridge *b, const LinuxHost *h, const FbDevice *d,
                       const TouchEvidence *e, int ok, const char *error) {
    fputs("{\"schema_version\":2,\"operation\":\"touch-test\",\"ok\":", out);
    fputs(ok ? "true" : "false", out); fputs(",\"error\":", out);
    if (error) json_string(out, error); else fputs("null", out);
    fputs(",\"profile\":\"imx6ul-1024x600\",\"fbdev\":", out); json_string(out, fbpath);
    fputs(",\"input\":{\"path\":", out); json_string(out, in->path);
    fputs(",\"name\":", out); json_string(out, in->name);
    fprintf(out, ",\"events\":%llu,\"frames\":%llu,\"syn_dropped\":%llu,\"resyncs\":%llu,"
            "\"disconnects\":%llu,\"reconnect_attempts\":%llu,\"reconnects\":%llu,\"budget_yields\":%llu,"
            "\"errno\":%d,\"cleanup_errno\":%d,\"kernel_monotonic\":%s},",
            (unsigned long long)in->events, (unsigned long long)in->frames,
            (unsigned long long)in->syn_dropped, (unsigned long long)in->resyncs,
            (unsigned long long)in->disconnects, (unsigned long long)in->reconnect_attempts,
            (unsigned long long)in->reconnects, (unsigned long long)in->budget_yields,
            in->system_errno, in->cleanup_errno, in->kernel_monotonic ? "true" : "false");
    fprintf(out, "\"bridge\":{\"ingested\":%llu,\"delivered\":%llu,\"hit_queries\":%llu,"
            "\"coalesced\":%llu,\"queue_peak\":%llu,\"cancellations\":%llu},"
            "\"host\":{\"turns\":%llu,\"renders\":%llu},\"display\":{\"presents\":%llu,"
            "\"vsync_enabled\":false,\"pan_enabled\":false,\"cleanup_errno\":%d},"
            "\"target_mask\":%u,\"all_five_targets_exercised\":%s,\"trace_rows\":%llu,"
            "\"transform\":{\"swap_xy\":false,\"invert_x\":false,\"invert_y\":false},"
            "\"transform_validation\":\"requires-asymmetric-visual-check\","
            "\"keyboard_enabled\":false,\"physical_touch_validated\":false}\n",
            (unsigned long long)b->ingested_frames, (unsigned long long)b->delivered_frames,
            (unsigned long long)b->hit_queries, (unsigned long long)b->coalesced_frames,
            (unsigned long long)b->queue_peak, (unsigned long long)b->resets,
            (unsigned long long)h->turns, (unsigned long long)h->renders,
            (unsigned long long)d->presents, d->cleanup_errno,
            e->target_mask, e->target_mask == 31 ? "true" : "false", (unsigned long long)e->rows);
    return !ferror(out);
}

int input_cli(int argc, char **argv) {
    const char *fbpath = "/dev/fb0", *input_dir = "/dev/input", *profile = NULL;
    const char *root = "assets", *output = NULL, *trace_path = NULL, *error = NULL;
    unsigned long ticks = 1200; unsigned seen = 0;
    int ok = 0, created = 0, trace_created = 0, signals = 0, shown = 0;
    FILE *report = stdout; TouchEvidence evidence = {0};
    FbDevice display = {0}; LinuxHost host = {0}; InputLive input = {0}; InputBridge bridge = {0};
    struct sigaction action = {0}, old_int = {0}, old_term = {0};
    uint64_t now = 0, retry_at = 0, deadline = 0; HostFrame frame;
    for (int i = 2; i < argc; i += 2) {
        unsigned bit;
        if (i + 1 >= argc) goto arguments;
        if (!strcmp(argv[i], "--fbdev")) { fbpath = argv[i+1]; bit = 1; }
        else if (!strcmp(argv[i], "--input-dir")) { input_dir = argv[i+1]; bit = 2; }
        else if (!strcmp(argv[i], "--profile")) { profile = argv[i+1]; bit = 4; }
        else if (!strcmp(argv[i], "--asset-root")) { root = argv[i+1]; bit = 8; }
        else if (!strcmp(argv[i], "--ticks")) { if (!unsigned_arg(argv[i+1], &ticks)) goto arguments; bit = 16; }
        else if (!strcmp(argv[i], "--output")) { output = argv[i+1]; bit = 32; }
        else if (!strcmp(argv[i], "--trace-output")) { trace_path = argv[i+1]; bit = 64; }
        else goto arguments;
        if ((seen & bit) || !argv[i+1][0]) goto arguments;
        seen |= bit;
    }
    if (!profile || strcmp(profile, "imx6ul-1024x600")) goto arguments;
    if (!open_report(output, &report, &created)) {
        fprintf(stderr, "TOUCH_REPORT_OPEN_FAILED errno=%d\n", errno); return 1;
    }
    if (trace_path && !open_report(trace_path, &evidence.trace, &trace_created)) {
        error = "TOUCH_TRACE_OPEN_FAILED"; goto cleanup;
    }
    if (evidence.trace) fputs("turn,sample,event_ns,guest_begin_ns,guest_end_ns,present_begin_ns,present_end_ns,contacts,cancelled,action_seq\n", evidence.trace);
    InputLiveConfig config = {.expected_name="ilitek_ts", .width=1024, .height=600,
        .swap_xy=0, .invert_x=0, .invert_y=0, .expected_raw_min=0, .expected_raw_max=16384, .expected_slots=10};
    if (!input_live_discover(&input, input_dir, &config)) { error = input.error; goto cleanup; }
    if (!fbdev_open(&display, fbpath, 1)) { error = display.error; goto cleanup; }
    if (!host_monotonic_ns(&now)) { error = "HOST_CLOCK_FAILED"; goto cleanup; }
    if (!host_open(&host, profile, root, "touch-scene.js", "display-font.bin", now) ||
        !input_bridge_init(&bridge, runtime_hit, NULL)) { error = "TOUCH_BOOT_FAILED"; goto cleanup; }
    if (!host_render(&host, &frame) || !fbdev_present(&display, &frame)) { error = "TOUCH_PRESENT_FAILED"; goto cleanup; }
    shown = 1;
    if (!host_monotonic_ns(&now)) { error = "HOST_CLOCK_FAILED"; goto cleanup; }
    host_clock_start(&host.clock, now);
    deadline = now + ((uint64_t)ticks / 60 + 30) * 1000000000ULL;
    action.sa_handler = stop_touch; sigemptyset(&action.sa_mask); interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int)) { error = "TOUCH_SIGNAL_FAILED"; goto cleanup; } signals = 1;
    if (sigaction(SIGTERM, &action, &old_term)) { error = "TOUCH_SIGNAL_FAILED"; goto cleanup; } signals = 2;
    while (host.turns < ticks && !interrupted) {
        if (!host_monotonic_ns(&now)) { error = "HOST_CLOCK_FAILED"; goto cleanup; }
        if (now >= deadline) { error = "TOUCH_WALL_DEADLINE"; goto cleanup; }
        int wait_ms = now >= host.clock.next_ns ? 0 : (int)((host.clock.next_ns-now+999999ULL)/1000000ULL);
        if (wait_ms > 17) wait_ms = 17;
        if (input.opened) {
            int ready = input_live_wait(&input, wait_ms);
            if (ready < 0 || (ready && input_live_drain(&input, bridge_sink, &bridge) < 0)) {
                const char *cause = input.error ? input.error : "INPUT_FAILED";
                fprintf(stderr, "TOUCH_INPUT_LOST cause=%s errno=%d\n", cause, input.system_errno);
                if (!cancel_guest(&host, &bridge, &display, &evidence)) { error = "TOUCH_CANCEL_FAILED"; goto cleanup; }
                if (!input.state.disconnected) { input_state_disconnect(&input.state); ++input.disconnects; }
                input_live_close(&input);
                retry_at = now + 500000000ULL;
            }
        } else {
            if (now >= retry_at) {
                if (input_live_reconnect(&input, input_dir))
                    fprintf(stderr, "TOUCH_INPUT_RECONNECTED path=%s held_suppressed=%d\n", input.path, input.state.suppress_until_all_up);
                retry_at = now + 500000000ULL;
            }
            if (!input.opened && wait_ms && poll(NULL, 0, wait_ms) < 0 && errno != EINTR) {
                error = "TOUCH_RETRY_WAIT_FAILED"; goto cleanup;
            }
        }
        if (!host_monotonic_ns(&now)) { error = "HOST_CLOCK_FAILED"; goto cleanup; }
        int due = host_clock_due(&host.clock, now);
        if (due < 0) { error = "HOST_CLOCK_REVERSED"; goto cleanup; }
        for (int i = 0; i < due && host.turns < ticks; ++i)
            if (!turn(&host, &bridge, &display, &evidence, 0)) { error = "TOUCH_TURN_FAILED"; goto cleanup; }
    }
    ok = !interrupted && input.opened && bridge.hit_queries > 0;
    if (!ok) error = interrupted ? "TOUCH_INTERRUPTED" : (input.opened ? "TOUCH_NO_INTERACTION" : "TOUCH_INPUT_OFFLINE");
cleanup:
    if (shown && !cancel_guest(&host, &bridge, &display, &evidence)) { ok = 0; error = "TOUCH_FINAL_CANCEL_FAILED"; }
    if (signals >= 1) sigaction(SIGINT, &old_int, NULL);
    if (signals >= 2) sigaction(SIGTERM, &old_term, NULL);
    input_live_close(&input); host_close(&host);
    int display_closed = fbdev_close(&display);
    if (input.cleanup_errno || !display_closed) { ok = 0; error = "TOUCH_CLEANUP_FAILED"; }
    if (trace_created) {
        int failed = ferror(evidence.trace);
        if (fflush(evidence.trace)) failed = 1;
        if (fclose(evidence.trace)) failed = 1;
        if (failed) { ok = 0; error = "TOUCH_TRACE_WRITE_FAILED"; }
    }
    int report_ok = report_json(report, fbpath, &input, &bridge, &host, &display, &evidence, ok, error);
    if (fflush(report)) report_ok = 0;
    if (created && fclose(report)) report_ok = 0;
    if (!report_ok) { fprintf(stderr, "TOUCH_REPORT_WRITE_FAILED\n"); return 1; }
    fprintf(stderr, "%s events=%llu input_frames=%llu guest_turns=%llu presents=%llu targets=%u reconnects=%llu physical_touch_validated=false\n",
            ok ? "TOUCH_TEST_OK" : (error ? error : "TOUCH_TEST_FAILED"),
            (unsigned long long)input.events, (unsigned long long)input.frames,
            (unsigned long long)host.turns, (unsigned long long)display.presents,
            evidence.target_mask, (unsigned long long)input.reconnects);
    return ok ? 0 : (interrupted ? 128 + interrupted : 1);
arguments:
    fprintf(stderr, "TOUCH_ARGUMENT_INVALID: --touch-test --profile imx6ul-1024x600 [--fbdev PATH] [--input-dir DIR] [--asset-root DIR] [--ticks 1..3600] [--output NEW.json] [--trace-output NEW.csv]\n");
    return 2;
}
