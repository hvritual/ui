#define _POSIX_C_SOURCE 200809L
#include "cli.h"
#include "bridge.h"
#include "live.h"
#include "../display/fbdev.h"
#include "../host.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop_touch(int number) { interrupted = number; }

static int unsigned_arg(const char *text, unsigned long min,
                        unsigned long max, unsigned long *out) {
    char *end = NULL;
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9') return 0;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || !end || *end || value < min || value > max) return 0;
    *out = value;
    return 1;
}

static int runtime_hit(void *context, float x, float y) {
    (void)context;
    return pocket_runtime_hit_test_bounds(x, y);
}

static int bridge_sink(void *context, const InputFrame *frame, uint64_t event_ns) {
    return input_bridge_ingest((InputBridge *)context, frame, event_ns);
}
static int bridge_source(void *context, PocketRuntimeContactsInput *out) {
    return input_bridge_next((InputBridge *)context, out, NULL);
}

static int open_report(const char *path, FILE **out, int *created) {
    if (!path) { *out = stdout; *created = 0; return 1; }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "w");
    if (!file) { int saved = errno; close(fd); unlink(path); errno = saved; return 0; }
    *out = file; *created = 1; return 1;
}

static int report_json(FILE *out, const char *fbdev, const InputLive *input,
                       const InputBridge *bridge, const LinuxHost *host,
                       const FbDevice *display, int ok) {
    if (!out || !input || !bridge || !host || !display) return 0;
    fprintf(out,
        "{\"schema_version\":1,\"operation\":\"touch-test\","
        "\"ok\":%s,\"profile\":\"imx6ul-1024x600\","
        "\"fbdev\":\"%s\",\"input\":{"
        "\"path\":\"%s\",\"name\":\"%s\","
        "\"protocol\":\"mt-protocol-b\","
        "\"raw_range\":[0,16384],"
        "\"transform\":{\"swap_xy\":false,\"invert_x\":false,\"invert_y\":false},"
        "\"events\":%llu,\"frames\":%llu,\"syn_dropped\":%llu,"
        "\"resyncs\":%llu,\"disconnects\":%llu},"
        "\"bridge\":{\"ingested\":%llu,\"delivered\":%llu,"
        "\"hit_queries\":%llu,\"coalesced\":%llu,\"queue_peak\":%llu},"
        "\"host\":{\"turns\":%llu,\"renders\":%llu},"
        "\"display\":{\"presents\":%llu,\"vsync_capability\":\"supported-not-enabled\"},"
        "\"physical_touch_validated\":false}\n",
        ok ? "true" : "false", fbdev,
        input->path, input->name,
        (unsigned long long)input->events,
        (unsigned long long)input->frames,
        (unsigned long long)input->syn_dropped,
        (unsigned long long)input->resyncs,
        (unsigned long long)input->disconnects,
        (unsigned long long)bridge->ingested_frames,
        (unsigned long long)bridge->delivered_frames,
        (unsigned long long)bridge->hit_queries,
        (unsigned long long)bridge->coalesced_frames,
        (unsigned long long)bridge->queue_peak,
        (unsigned long long)host->turns,
        (unsigned long long)host->renders,
        (unsigned long long)display->presents);
    return !ferror(out);
}

int input_cli(int argc, char **argv) {
    const char *fbpath = "/dev/fb0";
    const char *input_dir = "/dev/input";
    const char *profile = NULL;
    const char *root = "assets";
    const char *output = NULL;
    unsigned long ticks = 1200;
    unsigned seen = 0;
    int ok = 0, created = 0, signals = 0;
    FILE *report = stdout;
    FbDevice display = {0};
    LinuxHost host = {0};
    InputLive input = {0};
    InputBridge bridge = {0};
    HostFrame frame;
    struct sigaction action = {0}, old_int, old_term;
    uint64_t now;

    for (int i = 2; i < argc; i += 2) {
        unsigned bit = 0;
        if (i + 1 >= argc) goto arguments;
        if (!strcmp(argv[i], "--fbdev")) { fbpath = argv[i + 1]; bit = 1; }
        else if (!strcmp(argv[i], "--input-dir")) { input_dir = argv[i + 1]; bit = 2; }
        else if (!strcmp(argv[i], "--profile")) { profile = argv[i + 1]; bit = 4; }
        else if (!strcmp(argv[i], "--asset-root")) { root = argv[i + 1]; bit = 8; }
        else if (!strcmp(argv[i], "--ticks")) {
            if (!unsigned_arg(argv[i + 1], 1, 3600, &ticks)) goto arguments;
            bit = 16;
        } else if (!strcmp(argv[i], "--output")) { output = argv[i + 1]; bit = 32; }
        else goto arguments;
        if ((seen & bit) || !argv[i + 1][0]) goto arguments;
        seen |= bit;
    }
    if (!profile || strcmp(profile, "imx6ul-1024x600")) goto arguments;

    if (!open_report(output, &report, &created)) {
        fprintf(stderr, "TOUCH_REPORT_OPEN_FAILED errno=%d\n", errno); return 1;
    }

    InputLiveConfig config = {
        .expected_name = "ilitek_ts",
        .width = 1024, .height = 600,
        .swap_xy = 0, .invert_x = 0, .invert_y = 0,
        .expected_raw_min = 0, .expected_raw_max = 16384,
        .expected_slots = 10,
    };

    if (!fbdev_open(&display, fbpath, 1)) goto cleanup;
    if (!host_monotonic_ns(&now)) { host.error = "HOST_CLOCK_FAILED"; goto cleanup; }
    if (!host_open(&host, profile, root, "touch-scene.js", "display-font.bin", now))
        goto cleanup;
    if (!input_bridge_init(&bridge, runtime_hit, NULL)) {
        host.error = "INPUT_BRIDGE_INIT_FAILED"; goto cleanup;
    }
    if (!input_live_discover(&input, input_dir, &config)) {
        host.error = input.error ? input.error : "INPUT_DEVICE_NOT_FOUND"; goto cleanup;
    }

    if (!host_render(&host, &frame) || !fbdev_present(&display, &frame)) goto cleanup;

    action.sa_handler = stop_touch;
    sigemptyset(&action.sa_mask);
    interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int)) goto cleanup;
    signals = 1;
    if (sigaction(SIGTERM, &action, &old_term)) goto cleanup;
    signals = 2;

    while (host.turns < ticks && !interrupted) {
        if (!host_monotonic_ns(&now)) { host.error = "HOST_CLOCK_FAILED"; goto cleanup; }

        if (now < host.clock.next_ns) {
            uint64_t delta = host.clock.next_ns - now;
            int wait_ms = (int)((delta + 999999ULL) / 1000000ULL);
            if (wait_ms > 17) wait_ms = 17;
            int ready = input_live_wait(&input, wait_ms);
            if (ready < 0) { host.error = input.error ? input.error : "INPUT_POLL_FAILED"; goto cleanup; }
            if (ready > 0 && input_live_drain(&input, bridge_sink, &bridge) < 0) {
                host.error = input.error ? input.error : "INPUT_READ_FAILED"; goto cleanup;
            }
        }

        if (!host_monotonic_ns(&now)) { host.error = "HOST_CLOCK_FAILED"; goto cleanup; }
        if (now >= host.clock.next_ns) {
            int due = host_pump_present_contacts(&host, now,
                                                 bridge_source, &bridge,
                                                 fbdev_present, &display);
            if (due < 0) goto cleanup;
        }
    }
    ok = !interrupted && host.turns >= ticks && bridge.ingested_frames > 0 && bridge.hit_queries > 0;

cleanup:
    if (signals >= 1) sigaction(SIGINT, &old_int, NULL);
    if (signals >= 2) sigaction(SIGTERM, &old_term, NULL);

    if (!ok && !host.error) {
        host.error = interrupted ? "TOUCH_TEST_INTERRUPTED" :
                     (input.error ? input.error :
                     (bridge.error ? bridge.error :
                     (display.error ? display.error : "TOUCH_TEST_FAILED")));
    }

    input_live_close(&input);
    host_close(&host);
    if (!fbdev_close(&display)) ok = 0;

    int report_ok = report_json(report, fbpath, &input, &bridge, &host, &display, ok);
    if (fflush(report)) report_ok = 0;
    if (created && fclose(report)) report_ok = 0;
    if (!report_ok) {
        if (created) unlink(output);
        fprintf(stderr, "TOUCH_REPORT_WRITE_FAILED\n");
        return 1;
    }

    fprintf(stderr,
            "%s input=%s events=%llu input_frames=%llu guest_turns=%llu presents=%llu "
            "physical_touch_validated=false\n",
            ok ? "TOUCH_TEST_OK" : (host.error ? host.error : "TOUCH_TEST_FAILED"),
            input.path[0] ? input.path : "none",
            (unsigned long long)input.events,
            (unsigned long long)input.frames,
            (unsigned long long)host.turns,
            (unsigned long long)display.presents);
    return ok ? 0 : (interrupted ? 128 + interrupted : 1);

arguments:
    fprintf(stderr,
            "TOUCH_ARGUMENT_INVALID: --touch-test --profile imx6ul-1024x600 "
            "[--fbdev PATH] [--input-dir DIR] [--asset-root DIR] "
            "[--ticks 1..3600] [--output NEW.json]\n");
    return 2;
}
