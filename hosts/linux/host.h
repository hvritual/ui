#ifndef COFFEE_LINUX_HOST_H
#define COFFEE_LINUX_HOST_H
#include <stddef.h>
#include <stdint.h>
#include "pocket_runtime.h"

/* One owning UI thread and one live runtime per process. Not a device driver. */
typedef enum { HOST_STOPPED, HOST_RUNNING, HOST_PAUSED, HOST_FAILED } HostState;
typedef struct { unsigned char *data; size_t length; } HostAsset;
typedef struct {
    uint64_t next_ns, last_ns, ticks, overruns;
    unsigned remainder;
    int paused;
} HostClock;
typedef struct {
    HostState state;
    HostClock clock;
    HostAsset script, pack;
    uint32_t width, height;
    uint64_t turns, renders;
    const char *error;
} LinuxHost;
typedef struct {
    const uint8_t *pixels; /* Borrowed until render, close or next host mutation. */
    uint32_t width, height, stride;
    size_t length;
} HostFrame;
typedef struct { size_t live_bytes, peak_bytes, live_blocks; } HostAllocStats;

int host_asset_read(const char *root, const char *name, size_t limit, HostAsset *out);
void host_asset_free(HostAsset *asset);
void host_clock_start(HostClock *clock, uint64_t now);
int host_clock_due(HostClock *clock, uint64_t now); /* -1 backwards clock; 0..4 ticks */
void host_clock_pause(HostClock *clock, int paused, uint64_t now);
int host_monotonic_ns(uint64_t *out);
int host_sleep_until(uint64_t when);
HostAllocStats host_alloc_stats(void);
void *pocket_host_alloc(size_t size);
void *pocket_host_realloc(void *ptr, size_t size);
void pocket_host_free(void *ptr);

/* Pass a zero-initialized host. Asset storage outlives the borrowed __pak. */
int host_open(LinuxHost *host, const char *profile, const char *root,
              const char *bundle, const char *pack, uint64_t now);
int host_turn(LinuxHost *host, const PocketRuntimeInput *input);
int host_pump(LinuxHost *host, uint64_t now); /* headless, no hardware input */
int host_render(LinuxHost *host, HostFrame *frame);
int host_pause(LinuxHost *host, int paused, uint64_t now);
void host_close(LinuxHost *host);
#endif
