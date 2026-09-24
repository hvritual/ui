#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include <lvgl/lvgl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POCKET_MAX_NODES 128
#define POCKET_NO_TIMER_SLEEP_MS 1000U

struct PocketLvglEngine {
    uint32_t width;
    uint32_t height;
    uint32_t partial_rows;
    lv_display_t *display;
    uint8_t *draw_buffer;
    uint32_t draw_buffer_bytes;
    lv_obj_t *nodes[POCKET_MAX_NODES];
    uint16_t next_node;
    int32_t last_bar[POCKET_MAX_NODES];
    char last_label[POCKET_MAX_NODES][64];
    PocketEngineMetrics metrics;
};

static uint32_t tick_ms(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    PocketLvglEngine *engine = lv_display_get_driver_data(display);
    (void)px_map;
    if(engine && area) {
        const uint64_t w = (uint64_t)(area->x2 - area->x1 + 1);
        const uint64_t h = (uint64_t)(area->y2 - area->y1 + 1);
        const uint64_t pixels = w * h;
        engine->metrics.flush_calls++;
        engine->metrics.flush_pixels += pixels;
        engine->metrics.flush_bytes += pixels * 4ULL;
        if(area->x1 <= 0 && area->y1 <= 0 &&
           area->x2 >= (int32_t)engine->width - 1 &&
           area->y2 >= (int32_t)engine->height - 1) {
            engine->metrics.full_screen_flushes++;
        }
    }
    lv_display_flush_ready(display);
}

static PocketNode keep(PocketLvglEngine *engine, lv_obj_t *obj) {
    if(!engine || !obj || engine->next_node >= POCKET_MAX_NODES) return 0;
    const PocketNode id = ++engine->next_node;
    engine->nodes[id - 1] = obj;
    engine->metrics.bridge_create_calls++;
    return id;
}

static lv_obj_t *node(PocketLvglEngine *engine, PocketNode id) {
    if(!engine || id == 0 || id > engine->next_node) return NULL;
    return engine->nodes[id - 1];
}

static void place(lv_obj_t *obj, int32_t x, int32_t y, int32_t width, int32_t height) {
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
}

PocketLvglEngine *pocket_engine_create(uint32_t width, uint32_t height, uint32_t partial_rows) {
    if(width == 0 || height == 0 || partial_rows == 0 || partial_rows > height) return NULL;
    PocketLvglEngine *engine = calloc(1, sizeof(*engine));
    if(!engine) return NULL;
    engine->width = width;
    engine->height = height;
    engine->partial_rows = partial_rows;
    for(size_t i = 0; i < POCKET_MAX_NODES; ++i) engine->last_bar[i] = -1;

    lv_init();
    lv_tick_set_cb(tick_ms);
    engine->display = lv_display_create((int32_t)width, (int32_t)height);
    if(!engine->display) goto fail;
    lv_display_set_color_format(engine->display, LV_COLOR_FORMAT_XRGB8888);
    const uint64_t bytes = (uint64_t)width * partial_rows * 4ULL;
    if(bytes > UINT32_MAX) goto fail;
    engine->draw_buffer = malloc((size_t)bytes);
    if(!engine->draw_buffer) goto fail;
    engine->draw_buffer_bytes = (uint32_t)bytes;
    lv_display_set_buffers(engine->display, engine->draw_buffer, NULL, engine->draw_buffer_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_driver_data(engine->display, engine);
    lv_display_set_flush_cb(engine->display, flush_cb);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf8f6f0), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    return engine;
fail:
    if(engine->display) lv_display_delete(engine->display);
    free(engine->draw_buffer);
    free(engine);
    lv_deinit();
    return NULL;
}

void pocket_engine_destroy(PocketLvglEngine *engine) {
    if(!engine) return;
    if(engine->display) lv_display_delete(engine->display);
    free(engine->draw_buffer);
    free(engine);
    lv_deinit();
}

PocketNode pocket_engine_box(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                             uint32_t rgb, int32_t radius) {
    if(!engine) return 0;
    lv_obj_t *obj = lv_obj_create(lv_screen_active());
    if(!obj) return 0;
    place(obj, x, y, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(rgb & 0xffffffU), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_scrollable(obj, false);
    return keep(engine, obj);
}

PocketNode pocket_engine_label(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width,
                               const char *text, uint32_t rgb) {
    if(!engine || !text) return 0;
    lv_obj_t *obj = lv_label_create(lv_screen_active());
    if(!obj) return 0;
    place(obj, x, y, width, 32);
    lv_obj_set_style_text_color(obj, lv_color_hex(rgb & 0xffffffU), 0);
    lv_label_set_text(obj, text);
    PocketNode id = keep(engine, obj);
    if(id) {
        strncpy(engine->last_label[id - 1], text, sizeof(engine->last_label[id - 1]) - 1);
        engine->last_label[id - 1][sizeof(engine->last_label[id - 1]) - 1] = 0;
    }
    return id;
}

PocketNode pocket_engine_button(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                                const char *text, uint32_t rgb) {
    if(!engine || !text) return 0;
    lv_obj_t *obj = lv_button_create(lv_screen_active());
    if(!obj) return 0;
    place(obj, x, y, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(rgb & 0xffffffU), 0);
    PocketNode id = keep(engine, obj);
    lv_obj_t *label = lv_label_create(obj);
    if(label) {
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }
    return id;
}

PocketNode pocket_engine_bar(PocketLvglEngine *engine, int32_t x, int32_t y, int32_t width, int32_t height,
                             uint32_t rgb) {
    if(!engine) return 0;
    lv_obj_t *obj = lv_bar_create(lv_screen_active());
    if(!obj) return 0;
    place(obj, x, y, width, height);
    lv_bar_set_range(obj, 0, 100);
    lv_bar_set_value(obj, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(obj, lv_color_hex(rgb & 0xffffffU), LV_PART_INDICATOR);
    PocketNode id = keep(engine, obj);
    if(id) engine->last_bar[id - 1] = 0;
    return id;
}

int pocket_engine_set_bar(PocketLvglEngine *engine, PocketNode id, int32_t value) {
    lv_obj_t *obj = node(engine, id);
    if(!obj || value < 0 || value > 100) return 0;
    engine->metrics.bridge_update_calls++;
    if(engine->last_bar[id - 1] == value) {
        engine->metrics.bridge_duplicate_updates++;
        return 1;
    }
    engine->last_bar[id - 1] = value;
    lv_bar_set_value(obj, value, LV_ANIM_OFF);
    return 1;
}

int pocket_engine_set_label(PocketLvglEngine *engine, PocketNode id, const char *text) {
    lv_obj_t *obj = node(engine, id);
    if(!obj || !text) return 0;
    engine->metrics.bridge_update_calls++;
    if(strncmp(engine->last_label[id - 1], text, sizeof(engine->last_label[id - 1])) == 0) {
        engine->metrics.bridge_duplicate_updates++;
        return 1;
    }
    strncpy(engine->last_label[id - 1], text, sizeof(engine->last_label[id - 1]) - 1);
    engine->last_label[id - 1][sizeof(engine->last_label[id - 1]) - 1] = 0;
    lv_label_set_text(obj, text);
    return 1;
}

uint32_t pocket_engine_pump(PocketLvglEngine *engine) {
    if(!engine) return POCKET_NO_TIMER_SLEEP_MS;
    const uint64_t flush_before = engine->metrics.flush_calls;
    const uint32_t wait = lv_timer_handler();
    engine->metrics.handler_calls++;
    if(engine->metrics.flush_calls == flush_before) engine->metrics.handler_no_flush++;
    if(wait == LV_NO_TIMER_READY) {
        engine->metrics.no_timer_ready++;
        return POCKET_NO_TIMER_SLEEP_MS;
    }
    if(wait <= 1U) engine->metrics.immediate_retries++;
    return wait;
}

void pocket_engine_reset_metrics(PocketLvglEngine *engine) {
    if(!engine) return;
    const uint64_t creates = engine->metrics.bridge_create_calls;
    memset(&engine->metrics, 0, sizeof(engine->metrics));
    engine->metrics.bridge_create_calls = creates;
}

PocketEngineMetrics pocket_engine_metrics(const PocketLvglEngine *engine) {
    PocketEngineMetrics zero = {0};
    return engine ? engine->metrics : zero;
}

uint32_t pocket_engine_width(const PocketLvglEngine *engine) { return engine ? engine->width : 0; }
uint32_t pocket_engine_height(const PocketLvglEngine *engine) { return engine ? engine->height : 0; }
