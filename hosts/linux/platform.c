#define _POSIX_C_SOURCE 200809L
#include "host.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int leaf_name(const char *s) {
    if (!s || !*s || !strcmp(s, ".") || !strcmp(s, "..") || strlen(s) > 128) return 0;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}
int host_asset_read(const char *root, const char *name, size_t limit, HostAsset *out) {
    struct stat st;
    int directory = -1, fd = -1, ok = 0;
    size_t used = 0;
    unsigned char *bytes = NULL, extra;
    if (!out || out->data || !root || !leaf_name(name) || limit == SIZE_MAX) return 0;
    directory = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) goto done;
    fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uintmax_t)st.st_size > limit) goto done;
    bytes = malloc((size_t)st.st_size + 1);
    if (!bytes) goto done;
    while (used < (size_t)st.st_size) {
        ssize_t n = read(fd, bytes + used, (size_t)st.st_size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        used += (size_t)n;
    }
    for (;;) {
        ssize_t n = read(fd, &extra, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n != 0) goto done;
        break;
    }
    bytes[used] = 0; /* QuickJS requires the byte after source to be NUL. */
    out->data = bytes; out->length = used; bytes = NULL; ok = 1;
done:
    free(bytes);
    if (fd >= 0) close(fd);
    if (directory >= 0) close(directory);
    return ok;
}
void host_asset_free(HostAsset *a) {
    if (a) { free(a->data); a->data = NULL; a->length = 0; }
}

static void advance(HostClock *c) {
    c->next_ns += 16666666;
    c->remainder += 40;
    if (c->remainder >= 60) { ++c->next_ns; c->remainder -= 60; }
}
void host_clock_start(HostClock *c, uint64_t now) {
    memset(c, 0, sizeof(*c)); c->next_ns = c->last_ns = now; advance(c);
}
int host_clock_due(HostClock *c, uint64_t now) {
    int count = 0;
    if (now < c->last_ns || now > UINT64_MAX - 1000000000ULL) return -1;
    c->last_ns = now;
    if (c->paused) return 0;
    while (now >= c->next_ns && count < 4) { advance(c); ++count; ++c->ticks; }
    if (now >= c->next_ns) {
        /* Explicit bounded catch-up: discard wall-time debt, never add core-only ticks. */
        ++c->overruns; c->next_ns = now; c->remainder = 0; advance(c);
    }
    return count;
}
void host_clock_pause(HostClock *c, int paused, uint64_t now) {
    c->paused = !!paused; c->last_ns = now; c->next_ns = now; c->remainder = 0; advance(c);
}
int host_monotonic_ns(uint64_t *out) {
    struct timespec t;
    if (!out || clock_gettime(CLOCK_MONOTONIC, &t) || t.tv_sec < 0) return 0;
    *out = (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec; return 1;
}
int host_sleep_until(uint64_t when) {
    struct timespec t = { (time_t)(when / 1000000000ULL), (long)(when % 1000000000ULL) };
    int result;
    do { result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL); } while (result == EINTR);
    return result == 0;
}

/* Core-only allocation accounting. This is NOT RSS, QuickJS heap or a process limit. */
typedef union { max_align_t alignment; struct { size_t bytes; } data; } Allocation;
static HostAllocStats allocation_stats;
void *pocket_host_alloc(size_t size) {
    Allocation *p;
    if (!size) size = 1;
    if (size > SIZE_MAX - sizeof(*p) || size > SIZE_MAX - allocation_stats.live_bytes) return NULL;
    p = malloc(sizeof(*p) + size);
    if (!p) return NULL;
    p->data.bytes = size; allocation_stats.live_bytes += size; ++allocation_stats.live_blocks;
    if (allocation_stats.live_bytes > allocation_stats.peak_bytes) allocation_stats.peak_bytes = allocation_stats.live_bytes;
    return p + 1;
}
void pocket_host_free(void *ptr) {
    if (ptr) {
        Allocation *p = (Allocation *)ptr - 1;
        allocation_stats.live_bytes -= p->data.bytes; --allocation_stats.live_blocks; free(p);
    }
}
void *pocket_host_realloc(void *ptr, size_t size) {
    Allocation *old, *next;
    size_t previous;
    if (!ptr) return pocket_host_alloc(size);
    if (!size) size = 1;
    old = (Allocation *)ptr - 1; previous = old->data.bytes;
    if (size > SIZE_MAX - sizeof(*old) || size > SIZE_MAX - (allocation_stats.live_bytes - previous)) return NULL;
    next = realloc(old, sizeof(*old) + size);
    if (!next) return NULL; /* The original allocation remains owned by the caller. */
    next->data.bytes = size; allocation_stats.live_bytes = allocation_stats.live_bytes - previous + size;
    if (allocation_stats.live_bytes > allocation_stats.peak_bytes) allocation_stats.peak_bytes = allocation_stats.live_bytes;
    return next + 1;
}
HostAllocStats host_alloc_stats(void) { return allocation_stats; }
