#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include "workload.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t ns_now(clockid_t clock_id) {
    struct timespec ts;
    if(clock_gettime(clock_id, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int sleep_ms(uint32_t ms) {
    struct timespec req = {(time_t)(ms / 1000U), (long)(ms % 1000U) * 1000000L};
    while(nanosleep(&req, &req) != 0) {
        if(errno != EINTR) return 0;
    }
    return 1;
}

static int write_report(const char *path, const char *scenario, uint32_t width, uint32_t height,
                        uint32_t duration_ms, uint64_t wall_ns, uint64_t cpu_ns,
                        uint64_t wakeups, uint64_t slept_ms, uint64_t progress_updates,
                        PocketEngineMetrics m) {
    FILE *f = fopen(path, "wb");
    if(!f) return 0;
    const double wall_s = wall_ns ? (double)wall_ns / 1000000000.0 : 0.0;
    const double cpu = wall_ns ? (double)cpu_ns * 100.0 / (double)wall_ns : 0.0;
    const double wakeup_hz = wall_s > 0.0 ? (double)wakeups / wall_s : 0.0;
    const double retry_ratio = m.handler_calls ? (double)m.immediate_retries / (double)m.handler_calls : 0.0;
    const double no_flush_ratio = m.handler_calls ? (double)m.handler_no_flush / (double)m.handler_calls : 0.0;
    fprintf(f,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"engine\": \"lvgl\",\n"
        "  \"scenario\": \"%s\",\n"
        "  \"profile\": {\"width\": %u, \"height\": %u, \"color_format\": \"XRGB8888\", \"render_mode\": \"partial\"},\n"
        "  \"requested_duration_ms\": %u,\n"
        "  \"wall_ns\": %" PRIu64 ",\n"
        "  \"cpu_ns\": %" PRIu64 ",\n"
        "  \"cpu_percent_one_core\": %.6f,\n"
        "  \"wakeups\": %" PRIu64 ",\n"
        "  \"wakeup_hz\": %.6f,\n"
        "  \"slept_ms\": %" PRIu64 ",\n"
        "  \"handler_calls\": %" PRIu64 ",\n"
        "  \"handler_no_flush\": %" PRIu64 ",\n"
        "  \"handler_no_flush_ratio\": %.6f,\n"
        "  \"no_timer_ready\": %" PRIu64 ",\n"
        "  \"immediate_retries\": %" PRIu64 ",\n"
        "  \"immediate_retry_ratio\": %.6f,\n"
        "  \"flush_calls\": %" PRIu64 ",\n"
        "  \"flush_pixels\": %" PRIu64 ",\n"
        "  \"flush_bytes\": %" PRIu64 ",\n"
        "  \"full_screen_flushes\": %" PRIu64 ",\n"
        "  \"bridge_create_calls\": %" PRIu64 ",\n"
        "  \"bridge_update_calls\": %" PRIu64 ",\n"
        "  \"bridge_duplicate_updates\": %" PRIu64 ",\n"
        "  \"progress_updates\": %" PRIu64 ",\n"
        "  \"warmup_excluded\": true,\n"
        "  \"hardware_performance_authority\": false\n"
        "}\n",
        scenario, width, height, duration_ms, wall_ns, cpu_ns, cpu, wakeups, wakeup_hz, slept_ms,
        m.handler_calls, m.handler_no_flush, no_flush_ratio, m.no_timer_ready, m.immediate_retries, retry_ratio,
        m.flush_calls, m.flush_pixels, m.flush_bytes, m.full_screen_flushes,
        m.bridge_create_calls, m.bridge_update_calls, m.bridge_duplicate_updates, progress_updates);
    return fclose(f) == 0;
}

int main(int argc, char **argv) {
    uint32_t width = 1024, height = 600, duration_ms = 1500;
    const char *scenario = "idle", *output = NULL;
    for(int i = 1; i < argc; ++i) {
        if(!strcmp(argv[i], "--width") && i + 1 < argc) width = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if(!strcmp(argv[i], "--height") && i + 1 < argc) height = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if(!strcmp(argv[i], "--duration-ms") && i + 1 < argc) duration_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if(!strcmp(argv[i], "--scenario") && i + 1 < argc) scenario = argv[++i];
        else if(!strcmp(argv[i], "--output") && i + 1 < argc) output = argv[++i];
        else { fprintf(stderr, "SPIKE_ARGUMENT_INVALID\n"); return 2; }
    }
    if(width != 1024 || (height != 600 && height != 800) || duration_ms < 500 || duration_ms > 10000 ||
       (strcmp(scenario, "idle") && strcmp(scenario, "progress")) || !output) {
        fprintf(stderr, "SPIKE_ARGUMENT_INVALID\n"); return 2;
    }

    PocketLvglEngine *engine = pocket_engine_create(width, height, 40);
    PocketCoffeeWorkload workload;
    if(!engine || !pocket_coffee_build(engine, &workload)) {
        fprintf(stderr, "SPIKE_INIT_FAILED\n");
        pocket_engine_destroy(engine);
        return 1;
    }

    /* Warm up outside the measurement window. Initial layout/render must not
     * make the idle phase look expensive, and it must not hide later redraws. */
    const uint64_t warm_end = ns_now(CLOCK_MONOTONIC) + 250000000ULL;
    while(ns_now(CLOCK_MONOTONIC) < warm_end) {
        uint32_t wait = pocket_engine_pump(engine);
        if(wait < 1U) wait = 1U;
        if(wait > 20U) wait = 20U;
        if(!sleep_ms(wait)) return 1;
    }
    pocket_engine_reset_metrics(engine);

    const uint64_t wall_start = ns_now(CLOCK_MONOTONIC);
    const uint64_t cpu_start = ns_now(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t wakeups = 0, slept = 0, progress_updates = 0;
    uint32_t last_progress_step = 0;

    for(;;) {
        const uint64_t now = ns_now(CLOCK_MONOTONIC);
        const uint64_t elapsed_ns = now - wall_start;
        const uint32_t elapsed_ms = (uint32_t)(elapsed_ns / 1000000ULL);
        if(elapsed_ms >= duration_ms) break;

        if(!strcmp(scenario, "progress")) {
            const uint32_t step = elapsed_ms / 100U;
            if(step != last_progress_step) {
                if(!pocket_coffee_progress_step(engine, &workload, elapsed_ms)) return 1;
                last_progress_step = step;
                progress_updates++;
            }
        }

        uint32_t wait = pocket_engine_pump(engine);
        wakeups++;
        if(wait < 1U) wait = 1U;
        if(wait > 1000U) wait = 1000U;

        if(!strcmp(scenario, "progress")) {
            const uint32_t until_progress = 100U - (elapsed_ms % 100U);
            if(until_progress < wait) wait = until_progress;
        }
        const uint32_t remaining = duration_ms - elapsed_ms;
        if(remaining < wait) wait = remaining;
        if(wait > 0U) {
            if(!sleep_ms(wait)) return 1;
            slept += wait;
        }
    }

    const uint64_t cpu_end = ns_now(CLOCK_PROCESS_CPUTIME_ID);
    const uint64_t wall_end = ns_now(CLOCK_MONOTONIC);
    const PocketEngineMetrics metrics = pocket_engine_metrics(engine);
    const int ok = write_report(output, scenario, width, height, duration_ms,
                                wall_end - wall_start, cpu_end - cpu_start,
                                wakeups, slept, progress_updates, metrics);
    printf("LVGL_SPIKE_OK scenario=%s width=%u height=%u handlers=%" PRIu64
           " flushes=%" PRIu64 " pixels=%" PRIu64 "\n",
           scenario, width, height, metrics.handler_calls, metrics.flush_calls, metrics.flush_pixels);
    pocket_engine_destroy(engine);
    return ok ? 0 : 1;
}
