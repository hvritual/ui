#define _GNU_SOURCE
#include "loaded.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    unsigned char *bytes;
    size_t length;
    unsigned references;
    PuiPackage manifest;
    PuiLoadedSnapshot snapshot;
} Loaded;

PuiPolicy pui_runtime_policy(unsigned height, int allow_unsigned) {
    return (PuiPolicy){PUI_RUNTIME_API, PUI_SDK_API,
        height == 600 ? PUI_TARGET_600 : height == 800 ? PUI_TARGET_800 : 0,
        PUI_CAP_ALL, PUI_MAX_HEAP, PUI_MAX_BYTES, !!allow_unsigned};
}
static int same_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
int pui_loaded_open(PuiLoadedPackage *out, const char *path,
                    const PuiPolicy *policy, const char **error) {
    const char *why = "PUI_LOAD_ARGUMENT";
    Loaded *p = NULL;
    int fd = -1;
    struct stat before, after;
    if (error) *error = why;
    if (!out || out->impl || !path || !*path || !policy) return 0;
    /* NONBLOCK prevents an unexpected FIFO from hanging before fstat. It does
     * not promise a hard I/O deadline for a regular file on a remote filesystem. */
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) { why = "PUI_LOAD_OPEN"; goto failed; }
    if (fstat(fd, &before) || !S_ISREG(before.st_mode)) { why = "PUI_LOAD_REGULAR"; goto failed; }
    if (before.st_size < (off_t)PUI_HEADER_SIZE || before.st_size > (off_t)PUI_MAX_BYTES) {
        why = "PUI_LOAD_SIZE"; goto failed;
    }
    p = calloc(1, sizeof(*p));
    if (!p) { why = "PUI_LOAD_MEMORY"; goto failed; }
    p->length = (size_t)before.st_size;
    p->bytes = mmap(NULL, p->length, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p->bytes == MAP_FAILED) { p->bytes = NULL; why = "PUI_LOAD_MEMORY"; goto failed; }
    size_t offset = 0;
    unsigned interruptions = 0;
    while (offset < p->length) {
        ssize_t n = read(fd, p->bytes + offset, p->length - offset);
        if (n < 0 && errno == EINTR && interruptions++ < 32) continue;
        if (n <= 0) { why = "PUI_LOAD_READ"; goto failed; }
        offset += (size_t)n;
    }
    unsigned char extra;
    ssize_t tail;
    do { tail = read(fd, &extra, 1); } while (tail < 0 && errno == EINTR && interruptions++ < 32);
    if (tail != 0 || fstat(fd, &after) || !same_file(&before, &after)) {
        why = "PUI_LOAD_CHANGED"; goto failed;
    }
    /* Linux close consumes the descriptor even on EINTR; never retry it. */
    int closed = close(fd); fd = -1;
    if (closed) { why = "PUI_LOAD_CLOSE"; goto failed; }
    if (mprotect(p->bytes, p->length, PROT_READ)) { why = "PUI_LOAD_PROTECT"; goto failed; }
    PuiStatus status = pui_validate(p->bytes, p->length, policy, &p->manifest);
    if (status != PUI_OK) { why = pui_status_name(status); goto failed; }
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";
    pui_sha256(p->bytes, p->length, digest);
    for (unsigned i = 0; i < 32; ++i) {
        p->snapshot.sha256[i*2] = hex[digest[i] >> 4];
        p->snapshot.sha256[i*2+1] = hex[digest[i] & 15];
    }
    p->snapshot.container_bytes = p->length;
    p->snapshot.capabilities = p->manifest.capabilities;
    p->snapshot.heap_bytes = p->manifest.heap_bytes;
    p->snapshot.asset_bytes = p->manifest.asset_bytes;
    p->references = 1; out->impl = p;
    if (error) *error = NULL;
    return 1;
failed:
    if (fd >= 0 && close(fd)) why = "PUI_LOAD_CLOSE";
    if (p) {
        if (p->bytes && munmap(p->bytes, p->length)) why = "PUI_LOAD_UNMAP";
        free(p);
    }
    if (error) *error = why;
    return 0;
}
int pui_loaded_retain(PuiLoadedPackage *out, const PuiLoadedPackage *source) {
    Loaded *p = source ? source->impl : NULL;
    if (!out || out->impl || !p || !p->references || p->references == UINT32_MAX) return 0;
    ++p->references; out->impl = p; return 1;
}
int pui_loaded_close(PuiLoadedPackage *package) {
    Loaded *p = package ? package->impl : NULL;
    if (!p) return 1;
    if (p->references > 1) { --p->references; package->impl = NULL; return 1; }
    if (munmap(p->bytes, p->length)) return 0;
    memset(p, 0, sizeof(*p)); free(p); package->impl = NULL; return 1;
}
const PuiPackage *pui_loaded_manifest(const PuiLoadedPackage *package) {
    const Loaded *p = package ? package->impl : NULL;
    return p ? &p->manifest : NULL;
}
const PuiFile *pui_loaded_file(const PuiLoadedPackage *package, const char *name) {
    const PuiPackage *p = pui_loaded_manifest(package);
    if (!p || !name) return NULL;
    for (uint32_t i = 0; i < p->file_count; ++i)
        if (!strcmp(p->files[i].name, name)) return &p->files[i];
    return NULL;
}
int pui_loaded_snapshot(const PuiLoadedPackage *package, PuiLoadedSnapshot *out) {
    const Loaded *p = package ? package->impl : NULL;
    if (out) memset(out, 0, sizeof(*out));
    if (!p || !out) return 0;
    *out = p->snapshot; return 1;
}
