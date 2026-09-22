#define _POSIX_C_SOURCE 200809L
#include "live_cli.h"
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

typedef struct {
    InputBridge *bridge;
} BridgeSource;

static void stop_live(int number) { interrupted = number; }

static int ticks_value(const char *text, unsigned long *value) {
    char *end = NULL;
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return !errno && end && !*end && *value > 0 && *value <= 7200;
}

static int hit_query(void *context, float x, float y) {
    (void)context;
    return pocket_runtime_hit_test_bounds(x, y);
}

static int ingest(void *context, const InputFrame *frame, uint64_t event_ns) {
    return input_bridge_ingest((InputBridge *)context, frame, event_ns);
}

static int sample(void *context, PocketRuntimeContactsInput *out) {
    BridgeSource *source = context;
    return input_bridge_next(source->bridge, out, NULL);
}

static int wait_ms(uint64_t now, uint64_t next) {
    if (now >= next) return 0;
    uint64_t delta = next - now;
    uint64_t ms = (delta + 999999ULL) / 1000000ULL;
    return ms > 1000ULL ? 1000 : (int)ms;
}

static void json_string(FILE *out, const char *text) {
    fputc('"', out);
    for (size_t i = 0; text && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
        else if (c < 32 || c >= 127) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static int report(FILE *out, int ok, const char *fbdev, const InputLive *input,
                  const InputBridge *bridge, const LinuxHost *host,
                  const FbDevice *display) {
    if (!out || !input || !bridge || !host || !display) return 0;
    fputs("{\"schema_version\":1,\"operation\":\"input-display-test\",\"status\":", out);
    json_string(out, ok ? "passed" : "failed");
    fputs(",\"fbdev\":", out); json_string(out, fbdev);
    fputs(",\"input_path\":", out); json_string(out, input->path);
    fputs(",\"input_name\":", out); json_string(out, input->name);
    fputs(",\"selector\":\"name+protocol-b-capabilities\","
          "\"profile\":\"imx6ul-1024x600\","
          "\"transform\":{\"swap_xy\":false,\"invert_x\":false,\"invert_y\":false},", out);
    fprintf(out,
            "\"events\":%llu,\"input_frames\":%llu,\"syn_dropped\":%llu,"
            "\"resyncs\":%llu,\"disconnects\":%llu,\"last_event_ns\":%llu,"
            "\"bridge_ingested\":%llu,\"bridge_delivered\":%llu,"
            "\"hit_queries\":%llu,\"bridge_queue_peak\":%llu,"
            "\"bridge_coalesced\":%llu,\"guest_turns\":%llu,"
            "\"renders\":%llu,\"presents\":%llu,"
            "\"vsync_capability\":\"admitted-not-enabled-by-p3\","
            "\"physical_touch_validated\":false,\"error\":",
            (unsigned long long)input->events,
            (unsigned long long)input->frames,
            (unsigned long long)input->syn_dropped,
            (unsigned long long)input->resyncs,
            (unsigned long long)input->disconnects,
            (unsigned long long)input->last_event_ns,
            (unsigned long long)bridge->ingested_frames,
            (unsigned long long)bridge->delivered_frames,
            (unsigned long long)bridge->hit_queries,
            (unsigned long long)bridge->queue_peak,
            (unsigned long long)bridge->coalesced_frames,
            (unsigned long long)host->turns,
            (unsigned long long)host->renders,
            (unsigned long long)display->presents);
    const char *error = input->error ? input->error :
                        (bridge->error ? bridge->error :
                        (host->error ? host->error : display->error));
    if (error) json_string(out, error); else fputs("null", out);
    fputs("}\n", out);
    return !ferror(out);
}

int input_live_cli(int argc, char **argv) {
    const char *fb_path = "/dev/fb0";
    const char *input_dir = "/dev/input";
    const char *profile = NULL;
    const char *root = NULL;
    const char *output = NULL;
    unsigned long ticks = 1200;
    unsigned seen = 0;
    int ok = 0, created = 0, signals = 0;

    LinuxHost host = {0};
    FbDevice display = {0};
    InputLive input = {0};
    InputBridge bridge = {0};
    BridgeSource source = {.bridge = &bridge};
    HostFrame frame;
    FILE *report_file = stdout;
    struct sigaction action = {0}, old_int = {0}, old_term = {0};
    uint64_t now = 0;

    for (int i = 2; i < argc; i += 2) {
        unsigned bit = 0;
        if (i + 1 >= argc) goto arguments;
        if (!strcmp(argv[i], "--fbdev")) { fb_path = argv[i + 1]; bit = 1; }
        else if (!strcmp(argv[i], "--input-dir")) { input_dir = argv[i + 1]; bit = 2; }
        else if (!strcmp(argv[i], "--profile")) { profile = argv[i + 1]; bit = 4; }
        else if (!strcmp(argv[i], "--asset-root")) { root = argv[i + 1]; bit = 8; }
        else if (!strcmp(argv[i], "--ticks")) {
            if (!ticks_value(argv[i + 1], &ticks)) goto arguments;
            bit = 16;
        } else if (!strcmp(argv[i], "--output")) { output = argv[i + 1]; bit = 32; }
        else goto arguments;
        if ((seen & bit) || !argv[i + 1][0]) goto arguments;
        seen |= bit;
    }

    if (!profile || strcmp(profile, "imx6ul-1024x600") || !root) goto arguments;

    if (output) {
        int fd = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            fprintf(stderr, "INPUT_REPORT_OPEN_FAILED errno=%d\n", errno);
            return 1;
        }
        created = 1;
        report_file = fdopen(fd, "w");
        if (!report_file) {
            int saved = errno;
            close(fd); unlink(output);
            fprintf(stderr, "INPUT_REPORT_OPEN_FAILED errno=%d\n", saved);
            return 1;
        }
    }

    if (!host_monotonic_ns(&now)) goto cleanup;
    if (!host_open(&host, profile, root, "input-live-scene.js", NULL, now))
        goto cleanup;

    if (!input_bridge_init(&bridge, hit_query, NULL)) {
        host.error = "INPUT_BRIDGE_INIT_FAILED";
        goto cleanup;
    }

    InputLiveConfig config = {
        .expected_name = "ilitek_ts",
        .width = 1024,
        .height = 600,
        .swap_xy = 0,
        .invert_x = 0,
        .invert_y = 0,
        .expected_raw_min = 0,
        .expected_raw_max = 16384,
        .expected_slots = 10,
    };
    if (!input_live_discover(&input, input_dir, &config)) goto cleanup;
    if (!fbdev_open(&display, fb_path, 1)) goto cleanup;

    if (!host_render(&host, &frame) || !fbdev_present(&display, &frame))
        goto cleanup;

    action.sa_handler = stop_live;
    sigemptyset(&action.sa_mask);
    interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int)) goto cleanup;
    signals = 1;
    if (sigaction(SIGTERM, &action, &old_term)) goto cleanup;
    signals = 2;

    while (host.turns < ticks && !interrupted) {
        if (!host_monotonic_ns(&now)) {
            host.error = "HOST_CLOCK_FAILED";
            goto cleanup;
        }
        int wait = input_live_wait(&input, wait_ms(now, host.clock.next_ns));
        if (wait < 0) goto cleanup;
        if (wait > 0 && input_live_drain(&input, ingest, &bridge) < 0) goto cleanup;

        if (!host_monotonic_ns(&now)) {
            host.error = "HOST_CLOCK_FAILED";
            goto cleanup;
        }
        if (host_pump_present_contacts(&host, now, sample, &source,
                                       fbdev_present, &display) < 0)
            goto cleanup;
    }

    ok = !interrupted && bridge.ingested_frames > 0 && bridge.delivered_frames > 0;

cleanup:
    if (signals >= 1) sigaction(SIGINT, &old_int, NULL);
    if (signals >= 2) sigaction(SIGTERM, &old_term, NULL);
    if (!ok && interrupted && !host.error) host.error = "INPUT_TEST_INTERRUPTED";

    if (input.opened) input_live_close(&input);
    host_close(&host);
    if (!fbdev_close(&display)) ok = 0;

    int report_ok = report(report_file, ok, fb_path, &input, &bridge, &host, &display);
    if (fflush(report_file)) report_ok = 0;
    if (created && fclose(report_file)) report_ok = 0;
    if (!report_ok) {
        if (created) unlink(output);
        fprintf(stderr, "INPUT_REPORT_WRITE_FAILED\n");
        return 1;
    }

    fprintf(stderr,
            "%s input=%s events=%llu frames=%llu turns=%llu renders=%llu presents=%llu physical_touch_validated=false\n",
            ok ? "INPUT_DISPLAY_OK" :
                 (input.error ? input.error :
                  (bridge.error ? bridge.error :
                   (host.error ? host.error :
                    (display.error ? display.error : "INPUT_DISPLAY_FAILED")))),
            input.path[0] ? input.path : "none",
            (unsigned long long)input.events,
            (unsigned long long)input.frames,
            (unsigned long long)host.turns,
            (unsigned long long)host.renders,
            (unsigned long long)display.presents);
    return ok ? 0 : (interrupted ? 128 + interrupted : 1);

arguments:
    fprintf(stderr,
            "INPUT_ARGUMENT_INVALID: --input-display-test --profile imx6ul-1024x600 "
            "--asset-root DIR [--fbdev PATH] [--input-dir DIR] [--ticks 1..7200] "
            "[--output NEW.json]\n");
    return 2;
}
