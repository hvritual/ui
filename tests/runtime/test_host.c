#define _POSIX_C_SOURCE 200809L
#include "host.h"
#include "pocket_ui_cabi.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int stages[1024], stage_count, failures;
void pocket_bench_stage(int stage) { if (stage_count < 1024) stages[stage_count++] = stage; }
#define EXPECT(x) do { if (!(x)) { fprintf(stderr, "TEST_FAILED %s:%d %s\n", __FILE__, __LINE__, #x); failures++; goto cleanup; } } while (0)
#define PASS(s) printf("PASS %s\n", s)
static int inspect(int op) {
    int32_t n = -999;
    if (!pocket_runtime_harness_call(op, 0, &n)) { ++failures; return -999; }
    return n;
}
static int pixel(const HostFrame *f, int x, int y, int r, int g, int b) {
    const uint8_t *p = f->pixels + (size_t)y * f->stride + (size_t)x * 4;
    return p[0] == b && p[1] == g && p[2] == r && p[3] == 255;
}
static int open_scene(LinuxHost *h, const char *root, const char *profile) {
    return host_open(h, profile, root, "scene.js", "scene.pak", 0) && pocket_runtime_harness_bind("inspect");
}
int main(int argc, char **argv) {
    LinuxHost h = {0}, other = {0}; HostFrame f;
    const char *root = argc > 1 ? argv[1] : "tests/runtime";
    int deliberate = argc > 2 && !strcmp(argv[2], "--intentional-failure");
    char temporary[] = "/tmp/pocket-host-XXXXXX", path[256];
    HostAsset a = {0}; void *allocation = NULL;
    int temp_exists = 0;
    for (int display = 0; display < 2; display++) {
        EXPECT(open_scene(&h, root, display ? "imx6ul-1024x800" : "imx6ul-1024x600"));
        EXPECT(host_render(&h, &f));
        EXPECT(f.width == 1024 && f.height == (display ? 800U : 600U));
        EXPECT(pixel(&f, 10, 10, deliberate ? 0 : 255, 0, 0));
        EXPECT(pixel(&f, 900, 500, 0x11, 0x22, 0x33));
        EXPECT(pocket_runtime_hit_test_bounds(10, 10) == inspect(3));
        PASS(display ? "render-1024x800" : "render-1024x600");
        EXPECT(host_render(&h, &f)); EXPECT(pocket_runtime_damage_pixels() == 0);
        if (!display) PASS("incremental-idle");
        stage_count = 0;
        EXPECT(host_turn(&h, NULL));
        EXPECT(stage_count == 4 && stages[0] == POCKET_BENCH_STAGE_JS &&
               stages[1] == POCKET_BENCH_STAGE_JOBS && stages[2] == POCKET_BENCH_STAGE_TICK && stages[3] == 0);
        EXPECT(inspect(1) == 1 && inspect(2) == 1);
        EXPECT(host_render(&h, &f)); EXPECT(pixel(&f, 100, 10, 0, 255, 0));
        if (!display) { PASS("frame-stage-order"); PASS("promise-layout-update"); }
        EXPECT(inspect(5) == 1); EXPECT(host_render(&h, &f)); EXPECT(pixel(&f, 10, 10, 0x11, 0x22, 0x33));
        if (!display) PASS("node-remove-destroy");
        host_close(&h); EXPECT(host_alloc_stats().live_bytes == 0);
    }
    EXPECT(open_scene(&h, root, "imx6ul-1024x800"));
    {
        PocketRuntimeInput touch = {.touch_down = 1, .touch_x = 1023, .touch_y = 799, .touch_hit = 0};
        EXPECT(host_turn(&h, &touch));
        EXPECT((uint32_t)inspect(4) == (0x80000000U | (799U << 10) | 1023U));
    }
    PASS("wide-touch-wire"); host_close(&h);
    EXPECT(open_scene(&h, root, "imx6ul-1024x600"));
    EXPECT(!host_open(&other, "imx6ul-1024x600", root, "scene.js", "scene.pak", 0));
    EXPECT(host_turn(&h, NULL)); PASS("exclusive-runtime"); host_close(&h);
    EXPECT(open_scene(&h, root, "imx6ul-1024x600"));
    for (uint64_t i = 1; i <= 60; i++) EXPECT(host_pump(&h, (i * 1000000000ULL + 59) / 60) == 1);
    EXPECT(h.turns == 60 && h.renders == 30 && inspect(1) == 60 && inspect(2) == 60);
    PASS("clock-60-turns-30-renders");
    EXPECT(host_pause(&h, 1, 1000000000)); EXPECT(host_pump(&h, 9000000000) == 0);
    EXPECT(h.turns == 60); EXPECT(host_pause(&h, 0, 9000000000));
    EXPECT(host_pump(&h, 9000000000) == 0); EXPECT(host_pump(&h, 9016666667) == 1);
    EXPECT(h.turns == 61); PASS("pause-resume");
    EXPECT(host_pump(&h, 10000000000) == 4); EXPECT(h.clock.overruns == 1); PASS("bounded-catchup");
    EXPECT(host_pump(&h, 1) == -1 && h.state == HOST_FAILED); PASS("backwards-clock"); host_close(&h);
    EXPECT(open_scene(&h, root, "imx6ul-1024x600"));
    {
        PocketRuntimeInput bad = {.buttons = 4};
        EXPECT(!host_turn(&h, &bad)); EXPECT(h.state == HOST_FAILED); EXPECT(!host_turn(&h, NULL));
        EXPECT(!strcmp(h.error, "HOST_GUEST_TURN_FAILED"));
    }
    host_close(&h); EXPECT(host_alloc_stats().live_bytes == 0); PASS("guest-error-cleanup");
    EXPECT(!host_open(&h, "unknown", root, "scene.js", "scene.pak", 0)); host_close(&h); PASS("unknown-profile");
    EXPECT(!host_open(&h, "imx6ul-1024x600", root, "syntax-error.js", NULL, 0));
    host_close(&h); EXPECT(host_alloc_stats().live_bytes == 0); PASS("syntax-error-cleanup");
    EXPECT(!host_open(&h, "imx6ul-1024x600", root, "missing-frame.js", NULL, 0));
    host_close(&h); EXPECT(host_alloc_stats().live_bytes == 0); PASS("missing-frame-cleanup");
    for (int i = 0; i < 100; ++i) {
        EXPECT(open_scene(&h, root, i % 2 ? "imx6ul-1024x800" : "imx6ul-1024x600"));
        EXPECT(host_turn(&h, NULL)); EXPECT(host_render(&h, &f)); host_close(&h); host_close(&h);
        EXPECT(host_alloc_stats().live_bytes == 0 && host_alloc_stats().live_blocks == 0);
    }
    PASS("lifecycle-100-zero-core-allocations");
    EXPECT(mkdtemp(temporary) != NULL); temp_exists = 1;
    snprintf(path, sizeof(path), "%s/link", temporary); EXPECT(!symlink("/etc/passwd", path));
    EXPECT(!host_asset_read(temporary, "link", 4096, &a));
    snprintf(path, sizeof(path), "%s/fifo", temporary); EXPECT(!mkfifo(path, 0600));
    EXPECT(!host_asset_read(temporary, "fifo", 4096, &a));
    EXPECT(!host_asset_read(root, "../scene.js", 4096, &a)); EXPECT(!host_asset_read(root, "/etc/passwd", 4096, &a));
    EXPECT(!host_asset_read(root, "scene.js", 1, &a)); EXPECT(!host_asset_read(root, "absent.js", 4096, &a));
    EXPECT(host_asset_read(root, "scene.pak", 4, &a)); EXPECT(a.length == 4 && a.data[2] == 3);
    host_asset_free(&a); PASS("bounded-assets-and-paths");
    allocation = pocket_host_alloc(32); EXPECT(allocation != NULL); memset(allocation, 7, 32);
    EXPECT((uintptr_t)allocation % _Alignof(max_align_t) == 0);
    EXPECT(!pocket_host_realloc(allocation, SIZE_MAX)); EXPECT(((unsigned char *)allocation)[0] == 7);
    { void *resized = pocket_host_realloc(allocation, 64); EXPECT(resized != NULL); allocation = resized; }
    EXPECT(((unsigned char *)allocation)[31] == 7);
    pocket_host_free(allocation); allocation = NULL;
    EXPECT(host_alloc_stats().live_bytes == 0); PASS("allocator-alignment-overflow");
cleanup:
    host_close(&h); host_close(&other); host_asset_free(&a); pocket_host_free(allocation);
    if (temp_exists) {
        snprintf(path, sizeof(path), "%s/link", temporary); unlink(path);
        snprintf(path, sizeof(path), "%s/fifo", temporary); unlink(path); rmdir(temporary);
    }
    if (failures) return 1;
    printf("RUNTIME_OK pointer_bits=%zu hardware_tested=false\n", sizeof(void *) * 8); return 0;
}
