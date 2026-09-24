#ifndef POCKET_LVGL_ADAPTER_H
#define POCKET_LVGL_ADAPTER_H

#include <stdint.h>

typedef struct PocketLvglEngine PocketLvglEngine;
typedef uint16_t PocketNode;

typedef struct {
    uint64_t handler_calls;
    uint64_t handler_no_flush;
    uint64_t no_timer_ready;
    uint64_t immediate_retries;
    uint64_t flush_calls;
    uint64_t flush_pixels;
    uint64_t flush_bytes;
    uint64_t full_screen_flushes;
    uint64_t bridge_create_calls;
    uint64_t bridge_update_calls;
    uint64_t bridge_duplicate_updates;
} PocketEngineMetrics;

PocketLvglEngine *pocket_engine_create(uint32_t width, uint32_t height, uint32_t partial_rows);
void pocket_engine_destroy(PocketLvglEngine *engine);

PocketNode pocket_engine_box(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                             uint32_t rgb, int32_t radius);
PocketNode pocket_engine_label(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width,
                               const char *text, uint32_t rgb);
PocketNode pocket_engine_button(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                                const char *text, uint32_t rgb);
PocketNode pocket_engine_bar(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                             uint32_t rgb);
int pocket_engine_set_bar(PocketLvglEngine *engine, PocketNode node, int32_t value);
int pocket_engine_set_label(PocketLvglEngine *engine, PocketNode node, const char *text);

/* Runs one LVGL timer turn and returns the recommended wait in ms.
 * 1000 means LVGL has no timer ready; callers may wake earlier for input/business events. */
uint32_t pocket_engine_pump(PocketLvglEngine *engine);
void pocket_engine_reset_metrics(PocketLvglEngine *engine);
PocketEngineMetrics pocket_engine_metrics(const PocketLvglEngine *engine);
uint32_t pocket_engine_width(const PocketLvglEngine *engine);
uint32_t pocket_engine_height(const PocketLvglEngine *engine);

#endif
