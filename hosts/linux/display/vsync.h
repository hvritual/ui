#ifndef COFFEE_LINUX_VSYNC_H
#define COFFEE_LINUX_VSYNC_H
#include <stdint.h>
#include <stdio.h>

#define VSYNC_MAX_SAMPLES 120U

typedef enum {
    VSYNC_STATUS_UNKNOWN = 0,
    VSYNC_STATUS_SUPPORTED,
    VSYNC_STATUS_UNSUPPORTED,
    VSYNC_STATUS_TIMEOUT,
    VSYNC_STATUS_INTERRUPTED,
    VSYNC_STATUS_ERROR
} VsyncStatus;

typedef struct {
    unsigned requested;
    unsigned completed;
    unsigned timeout_ms;
    int last_errno;
    VsyncStatus status;
    uint64_t samples_ns[VSYNC_MAX_SAMPLES];
} VsyncProbe;

/* Read-only capability probe. It never changes mode, pans, maps or writes framebuffer memory. */
int vsync_probe_fd(int fd, unsigned count, unsigned timeout_ms, VsyncProbe *out);
int vsync_report(FILE *out, const char *path, const VsyncProbe *probe);
const char *vsync_status_name(VsyncStatus status);
int vsync_cli(int argc, char **argv);
#endif
