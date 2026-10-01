#ifndef COFFEE_LINUX_HOST_H
#define COFFEE_LINUX_HOST_H
#include <stddef.h>
#include <stdint.h>
#include "pocket_runtime.h"
#include "frame.h"

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
    uint64_t presented_frames, clean_frames_skipped;
    int presentation_valid;
    const char *error;
} LinuxHost;
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
/* Copies a bounded platform-owned script. Application data is parsed, never
 * concatenated as executable source into this private rendering context. */
int host_open_source(LinuxHost *host, const char *profile, const char *root,
                     const char *source, size_t length, const char *pack, uint64_t now);
/* Copies both buffers. The private renderer's borrowed pack remains owned by
 * this host until pocket_runtime_shutdown, independently of caller lifetime. */
int host_open_buffers(LinuxHost *host, const char *profile, const char *source,
                      size_t source_length, const unsigned char *pack_bytes,
                      size_t pack_length, uint64_t now);
int host_turn(LinuxHost *host, const PocketRuntimeInput *input);
int host_turn_contacts(LinuxHost *host, const PocketRuntimeContactsInput *input);
typedef int (*HostContactsSource)(void *context, PocketRuntimeContactsInput *out);
int host_pump(LinuxHost *host, uint64_t now); /* headless, no hardware input */
int host_render(LinuxHost *host, HostFrame *frame);
/* Call synchronously after host_render, before any runtime mutation.
 * Only successful presentation validates the destination. Force on exposure,
 * destination replacement or recovery; zero damage is not a framebuffer lease. */
int host_present_latest(LinuxHost *host, const HostFrame *frame, int force,
                        HostPresenter present, void *context);
int host_pump_present(LinuxHost *host, uint64_t now, HostPresenter present, void *context);
int host_pump_present_contacts(LinuxHost *host, uint64_t now,
                               HostContactsSource source, void *source_context,
                               HostPresenter present, void *present_context);
int host_pause(LinuxHost *host, int paused, uint64_t now);
void host_close(LinuxHost *host);
#endif
