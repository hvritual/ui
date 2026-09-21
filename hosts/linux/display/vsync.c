#define _POSIX_C_SOURCE 200809L
#include "vsync.h"
#include "fbdev.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif

typedef struct {
    unsigned index;
    int rc;
    int error_number;
    uint64_t elapsed_ns;
} VsyncRecord;

static uint64_t ns_diff(struct timespec start, struct timespec end) {
    uint64_t a = (uint64_t)start.tv_sec * 1000000000ULL + (uint64_t)start.tv_nsec;
    uint64_t b = (uint64_t)end.tv_sec * 1000000000ULL + (uint64_t)end.tv_nsec;
    return b >= a ? b - a : 0;
}

static int write_record(int fd, const VsyncRecord *record) {
    const unsigned char *p = (const unsigned char *)record;
    size_t left = sizeof(*record);
    while (left) {
        ssize_t n = write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += (size_t)n; left -= (size_t)n;
    }
    return 1;
}

static int read_record(int fd, VsyncRecord *record) {
    unsigned char *p = (unsigned char *)record;
    size_t left = sizeof(*record);
    while (left) {
        ssize_t n = read(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += (size_t)n; left -= (size_t)n;
    }
    return 1;
}

static int unsupported_errno(int number) {
    return number == ENOTTY || number == EINVAL || number == ENOSYS || number == EOPNOTSUPP;
}

const char *vsync_status_name(VsyncStatus status) {
    switch (status) {
        case VSYNC_STATUS_SUPPORTED: return "supported";
        case VSYNC_STATUS_UNSUPPORTED: return "unsupported";
        case VSYNC_STATUS_TIMEOUT: return "timeout";
        case VSYNC_STATUS_INTERRUPTED: return "interrupted";
        case VSYNC_STATUS_ERROR: return "error";
        default: return "unknown";
    }
}

int vsync_probe_fd(int fd, unsigned count, unsigned timeout_ms, VsyncProbe *out) {
    int pipefd[2] = {-1, -1};
    pid_t child = -1;
    int status = 0, ok = 0;
    if (!out || fd < 0 || count == 0 || count > VSYNC_MAX_SAMPLES || timeout_ms == 0 || timeout_ms > 1000) return 0;
    memset(out, 0, sizeof(*out));
    out->requested = count; out->timeout_ms = timeout_ms; out->status = VSYNC_STATUS_UNKNOWN;
    if (pipe(pipefd)) { out->status = VSYNC_STATUS_ERROR; out->last_errno = errno; return 1; }
    child = fork();
    if (child < 0) { out->status = VSYNC_STATUS_ERROR; out->last_errno = errno; goto cleanup; }
    if (child == 0) {
        close(pipefd[0]);
        for (unsigned i = 0; i < count; ++i) {
            struct timespec before = {0}, after = {0};
            __u32 crtc = 0;
            VsyncRecord record = {.index = i, .rc = -1, .error_number = 0, .elapsed_ns = 0};
            if (clock_gettime(CLOCK_MONOTONIC, &before)) { record.error_number = errno; write_record(pipefd[1], &record); _exit(1); }
            errno = 0;
            record.rc = ioctl(fd, FBIO_WAITFORVSYNC, &crtc);
            record.error_number = record.rc == 0 ? 0 : errno;
            if (clock_gettime(CLOCK_MONOTONIC, &after)) { record.rc = -1; record.error_number = errno; }
            else record.elapsed_ns = ns_diff(before, after);
            if (!write_record(pipefd[1], &record)) _exit(1);
            if (record.rc != 0) break;
        }
        close(pipefd[1]); _exit(0);
    }
    close(pipefd[1]); pipefd[1] = -1;
    for (unsigned i = 0; i < count; ++i) {
        struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
        int polled;
        do { polled = poll(&pfd, 1, (int)timeout_ms); } while (polled < 0 && errno == EINTR);
        if (polled == 0) {
            out->status = VSYNC_STATUS_TIMEOUT; out->last_errno = ETIMEDOUT;
            kill(child, SIGKILL); waitpid(child, &status, 0); child = -1; ok = 1; goto cleanup;
        }
        if (polled < 0 || !(pfd.revents & (POLLIN | POLLHUP))) {
            out->status = VSYNC_STATUS_ERROR; out->last_errno = polled < 0 ? errno : EIO;
            kill(child, SIGKILL); waitpid(child, &status, 0); child = -1; ok = 1; goto cleanup;
        }
        VsyncRecord record;
        if (!read_record(pipefd[0], &record) || record.index != i) {
            out->status = VSYNC_STATUS_ERROR; out->last_errno = EIO;
            kill(child, SIGKILL); waitpid(child, &status, 0); child = -1; ok = 1; goto cleanup;
        }
        if (record.rc == 0) {
            out->samples_ns[out->completed++] = record.elapsed_ns;
            continue;
        }
        out->last_errno = record.error_number;
        if (unsupported_errno(record.error_number)) out->status = VSYNC_STATUS_UNSUPPORTED;
        else if (record.error_number == EINTR) out->status = VSYNC_STATUS_INTERRUPTED;
        else out->status = VSYNC_STATUS_ERROR;
        waitpid(child, &status, 0); child = -1; ok = 1; goto cleanup;
    }
    if (waitpid(child, &status, 0) < 0) { out->status = VSYNC_STATUS_ERROR; out->last_errno = errno; child = -1; ok = 1; goto cleanup; }
    child = -1;
    out->status = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? VSYNC_STATUS_SUPPORTED : VSYNC_STATUS_ERROR;
    if (out->status == VSYNC_STATUS_ERROR && !out->last_errno) out->last_errno = EIO;
    ok = 1;
cleanup:
    if (child > 0) { kill(child, SIGKILL); waitpid(child, NULL, 0); }
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (pipefd[1] >= 0) close(pipefd[1]);
    return ok;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t aa = *(const uint64_t *)a, bb = *(const uint64_t *)b;
    return aa < bb ? -1 : aa > bb ? 1 : 0;
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

int vsync_report(FILE *out, const char *path, const VsyncProbe *probe) {
    uint64_t sorted[VSYNC_MAX_SAMPLES] = {0};
    if (!out || !probe) return 0;
    if (probe->completed) {
        memcpy(sorted, probe->samples_ns, probe->completed * sizeof(sorted[0]));
        qsort(sorted, probe->completed, sizeof(sorted[0]), cmp_u64);
    }
    fprintf(out, "{\"schema_version\":1,\"operation\":\"vsync-probe\",\"device\":"); json_string(out, path);
    fprintf(out, ",\"ioctl\":\"FBIO_WAITFORVSYNC\",\"status\":\"%s\",\"requested\":%u,\"completed\":%u,"
                 "\"timeout_ms\":%u,\"last_errno\":%d,\"writes_framebuffer\":false,\"mode_changed_by_host\":false,\"samples_ns\":[",
            vsync_status_name(probe->status), probe->requested, probe->completed, probe->timeout_ms, probe->last_errno);
    for (unsigned i = 0; i < probe->completed; ++i) fprintf(out, "%s%llu", i ? "," : "", (unsigned long long)probe->samples_ns[i]);
    fputs("],\"stats_ns\":", out);
    if (!probe->completed) fputs("null", out);
    else {
        unsigned mid = probe->completed / 2;
        uint64_t median = probe->completed % 2 ? sorted[mid] : (sorted[mid - 1] / 2ULL + sorted[mid] / 2ULL + ((sorted[mid - 1] & 1ULL) && (sorted[mid] & 1ULL)));
        unsigned p95_index = (95U * probe->completed + 99U) / 100U;
        if (p95_index) --p95_index;
        fprintf(out, "{\"min\":%llu,\"median\":%llu,\"p95\":%llu,\"max\":%llu}",
                (unsigned long long)sorted[0], (unsigned long long)median,
                (unsigned long long)sorted[p95_index], (unsigned long long)sorted[probe->completed - 1]);
    }
    fputs("}\n", out);
    return !ferror(out);
}

static int bounded_uint(const char *text, unsigned min, unsigned max, unsigned *out) {
    char *end = NULL; unsigned long value;
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    errno = 0; value = strtoul(text, &end, 10);
    if (errno || !end || *end || value < min || value > max) return 0;
    *out = (unsigned)value; return 1;
}

int vsync_cli(int argc, char **argv) {
    const char *path = "/dev/fb0", *output = NULL;
    unsigned count = 20, timeout_ms = 100, seen = 0;
    FILE *report = stdout; int created = 0, result = 1;
    FbDevice device = {0}; VsyncProbe probe = {0};
    for (int i = 2; i < argc; i += 2) {
        unsigned bit = 0;
        if (i + 1 >= argc) goto arguments;
        if (!strcmp(argv[i], "--fbdev")) { path = argv[i + 1]; bit = 1; }
        else if (!strcmp(argv[i], "--output")) { output = argv[i + 1]; bit = 2; }
        else if (!strcmp(argv[i], "--count")) { if (!bounded_uint(argv[i + 1], 1, VSYNC_MAX_SAMPLES, &count)) goto arguments; bit = 4; }
        else if (!strcmp(argv[i], "--timeout-ms")) { if (!bounded_uint(argv[i + 1], 1, 1000, &timeout_ms)) goto arguments; bit = 8; }
        else goto arguments;
        if ((seen & bit) || !argv[i + 1][0]) goto arguments;
        seen |= bit;
    }
    if (output) {
        int fd = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { fprintf(stderr, "VSYNC_REPORT_OPEN_FAILED errno=%d\n", errno); return 1; }
        created = 1; report = fdopen(fd, "w");
        if (!report) { int saved = errno; close(fd); unlink(output); fprintf(stderr, "VSYNC_REPORT_OPEN_FAILED errno=%d\n", saved); return 1; }
    }
    if (!fbdev_open(&device, path, 0)) {
        probe.status = VSYNC_STATUS_ERROR; probe.last_errno = device.system_errno; probe.requested = count; probe.timeout_ms = timeout_ms;
        goto finish;
    }
    if (!vsync_probe_fd(device.fd, count, timeout_ms, &probe)) { probe.status = VSYNC_STATUS_ERROR; probe.last_errno = EIO; }
finish:
    if (!fbdev_close(&device) && probe.status != VSYNC_STATUS_ERROR) { probe.status = VSYNC_STATUS_ERROR; probe.last_errno = device.cleanup_errno; }
    if (!vsync_report(report, path, &probe) || fflush(report) || (created && fclose(report))) {
        if (created) unlink(output);
        fprintf(stderr, "VSYNC_REPORT_WRITE_FAILED\n");
        return 1;
    }
    fprintf(stderr, "VSYNC_PROBE_%s requested=%u completed=%u errno=%d writes_framebuffer=false\n",
            probe.status == VSYNC_STATUS_SUPPORTED ? "SUPPORTED" :
            probe.status == VSYNC_STATUS_UNSUPPORTED ? "UNSUPPORTED" :
            probe.status == VSYNC_STATUS_TIMEOUT ? "TIMEOUT" :
            probe.status == VSYNC_STATUS_INTERRUPTED ? "INTERRUPTED" : "ERROR",
            probe.requested, probe.completed, probe.last_errno);
    result = probe.status == VSYNC_STATUS_ERROR ? 1 : 0;
    return result;
arguments:
    fprintf(stderr, "VSYNC_ARGUMENT_INVALID: --probe-vsync [--fbdev PATH] [--count 1..120] [--timeout-ms 1..1000] [--output NEW.json]\n");
    return 2;
}
