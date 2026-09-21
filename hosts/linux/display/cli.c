#define _POSIX_C_SOURCE 200809L
#include "cli.h"
#include "fbdev.h"
#include "../host.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop_display(int number) { interrupted = number; }
static int ticks_value(const char *text, unsigned long *value) {
    char *end;
    if (!text[0]) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    errno = 0; *value = strtoul(text, &end, 10);
    return !errno && !*end && *value > 0 && *value <= 600;
}
int display_cli(int argc, char **argv) {
    const int probe = !strcmp(argv[1], "--probe-display");
    const char *path = "/dev/fb0", *profile = NULL, *root = "fixtures", *output = NULL;
    unsigned long ticks = 300;
    unsigned seen = 0;
    FbDevice device = {0}; LinuxHost host = {0}; HostFrame frame;
    FILE *report = stdout;
    struct sigaction action = {0}, old_int, old_term;
    int signals = 0, ok = 0, created = 0;
    uint64_t now;
    for (int i = 2; i < argc; i += 2) {
        unsigned bit = 0;
        if (i + 1 >= argc) goto arguments;
        if (!strcmp(argv[i], "--fbdev")) { path = argv[i + 1]; bit = 1; }
        else if (!strcmp(argv[i], "--output")) { output = argv[i + 1]; bit = 2; }
        else if (!probe && !strcmp(argv[i], "--profile")) { profile = argv[i + 1]; bit = 4; }
        else if (!probe && !strcmp(argv[i], "--asset-root")) { root = argv[i + 1]; bit = 8; }
        else if (!probe && !strcmp(argv[i], "--ticks")) {
            if (!ticks_value(argv[i + 1], &ticks)) goto arguments;
            bit = 16;
        } else goto arguments;
        if ((seen & bit) || !argv[i + 1][0]) goto arguments;
        seen |= bit;
    }
    if (!probe && (!profile || (strcmp(profile, "imx6ul-1024x600") && strcmp(profile, "imx6ul-1024x800")))) goto arguments;
    if (output) {
        int fd = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { fprintf(stderr, "DISPLAY_REPORT_OPEN_FAILED errno=%d\n", errno); return 1; }
        created = 1; report = fdopen(fd, "w");
        if (!report) { int saved = errno; close(fd); unlink(output); fprintf(stderr, "DISPLAY_REPORT_OPEN_FAILED errno=%d\n", saved); return 1; }
    }
    if (!fbdev_open(&device, path, !probe)) goto cleanup;
    if (probe) { ok = 1; goto cleanup; }
    if (!host_monotonic_ns(&now)) { device.error = "HOST_CLOCK_FAILED"; goto cleanup; }
    if (!host_open(&host, profile, root, "display-scene.js", "display-font.bin", now)) {
        device.error = host.error ? host.error : "HOST_BOOT_FAILED"; goto cleanup;
    }
    if (!host_render(&host, &frame) || !fbdev_present(&device, &frame)) goto cleanup;
    action.sa_handler = stop_display; sigemptyset(&action.sa_mask); interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int)) { device.error = "DISPLAY_SIGNAL_SETUP_FAILED"; device.system_errno = errno; goto cleanup; }
    signals = 1;
    if (sigaction(SIGTERM, &action, &old_term)) { device.error = "DISPLAY_SIGNAL_SETUP_FAILED"; device.system_errno = errno; goto cleanup; }
    signals = 2;
    while (host.turns < ticks && !interrupted) {
        if (!host_sleep_until(host.clock.next_ns) || !host_monotonic_ns(&now) ||
            host_pump_present(&host, now, fbdev_present, &device) < 0) goto cleanup;
    }
    ok = !interrupted;
cleanup:
    if (signals >= 1) sigaction(SIGINT, &old_int, NULL);
    if (signals >= 2) sigaction(SIGTERM, &old_term, NULL);
    if (!ok && !device.error) device.error = interrupted ? "DISPLAY_INTERRUPTED" :
        (host.error ? host.error : "DISPLAY_TEST_FAILED");
    host_close(&host);
    if (!fbdev_close(&device)) ok = 0;
    int report_ok = fbdev_report(report, path, &device);
    if (fflush(report)) report_ok = 0;
    if (created && fclose(report)) report_ok = 0;
    if (!report_ok) { if (created) unlink(output); fprintf(stderr, "DISPLAY_REPORT_WRITE_FAILED\n"); return 1; }
    fprintf(stderr, "%s presents=%llu physical_panel_validated=false\n",
            ok ? (probe ? "DISPLAY_PROBE_OK" : "DISPLAY_OK") : (device.error ? device.error : "DISPLAY_FAILED"),
            (unsigned long long)device.presents);
    return ok ? 0 : (interrupted ? 128 + interrupted : 1);
arguments:
    fprintf(stderr, "DISPLAY_ARGUMENT_INVALID: --probe-display [--fbdev PATH] [--output NEW.json] | "
            "--display-test --profile imx6ul-1024x600|imx6ul-1024x800 [--fbdev PATH] "
            "[--asset-root DIR] [--ticks 1..600] [--output NEW.json]\n");
    return 2;
}
