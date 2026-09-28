#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include <lvgl/lvgl.h>
#include <lvgl/drivers/display/lv_linux_fbdev.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define POCKET_MAX_NODES 128
#define POCKET_NO_TIMER_SLEEP_MS 1000U

struct PocketLvglEngine {
    uint32_t width;
    uint32_t height;
    uint32_t partial_rows;
    lv_display_t *display;
    uint8_t *draw_buffer;
    uint32_t draw_buffer_bytes;
    lv_display_flush_cb_t delegate_flush;
    int framebuffer_active;
    int lock_fd;
    lv_obj_t *nodes[POCKET_MAX_NODES];
    uint16_t next_node;
    int32_t last_bar[POCKET_MAX_NODES];
    char last_label[POCKET_MAX_NODES][64];
    PocketEngineMetrics metrics;
};

static uint64_t cpu_ns(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint32_t tick_ms(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

static void record_flush(PocketLvglEngine *engine, const lv_area_t *area) {
    if(!engine || !area) return;
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

static void counting_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    PocketLvglEngine *engine = lv_display_get_user_data(display);
    (void)px_map;
    record_flush(engine, area);
    lv_display_flush_ready(display);
}

static void forwarding_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    PocketLvglEngine *engine = lv_display_get_user_data(display);
    if(!engine || !engine->delegate_flush) {
        lv_display_flush_ready(display);
        return;
    }
    record_flush(engine, area);
    const uint64_t start = cpu_ns();
    engine->delegate_flush(display, area, px_map);
    const uint64_t end = cpu_ns();
    if(end >= start) engine->metrics.flush_cpu_ns += end - start;
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

static PocketLvglEngine *alloc_engine(uint32_t width, uint32_t height, uint32_t partial_rows) {
    PocketLvglEngine *engine = calloc(1, sizeof(*engine));
    if(!engine) return NULL;
    engine->width = width;
    engine->height = height;
    engine->partial_rows = partial_rows;
    engine->lock_fd = -1;
    for(size_t i = 0; i < POCKET_MAX_NODES; ++i) engine->last_bar[i] = -1;
    return engine;
}

static void init_screen(PocketLvglEngine *engine) {
    lv_display_set_default(engine->display);
    lv_tick_set_cb(tick_ms);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf8f6f0), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
}

PocketLvglEngine *pocket_engine_create(uint32_t width, uint32_t height, uint32_t partial_rows) {
    if(width == 0 || height == 0 || partial_rows == 0 || partial_rows > height) return NULL;
    PocketLvglEngine *engine = alloc_engine(width, height, partial_rows);
    if(!engine) return NULL;

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
    lv_display_set_user_data(engine->display, engine);
    lv_display_set_flush_cb(engine->display, counting_flush_cb);
    init_screen(engine);
    return engine;
fail:
    if(engine->display) lv_display_delete(engine->display);
    free(engine->draw_buffer);
    free(engine);
    lv_deinit();
    return NULL;
}

static int preflight_fbdev(PocketLvglEngine *engine, const char *path) {
    struct stat st;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if(!engine || !path) return 0;
    engine->lock_fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(engine->lock_fd < 0) return 0;
    if(fstat(engine->lock_fd, &st) != 0 || !S_ISCHR(st.st_mode) || major(st.st_rdev) != 29) return 0;
    if(flock(engine->lock_fd, LOCK_EX | LOCK_NB) != 0) return 0;
    memset(&fix, 0, sizeof(fix));
    memset(&var, 0, sizeof(var));
    if(ioctl(engine->lock_fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
       ioctl(engine->lock_fd, FBIOGET_VSCREENINFO, &var) != 0) return 0;

    if(var.xres != engine->width || var.yres != engine->height ||
       var.xres_virtual < var.xres || var.yres_virtual < var.yres ||
       var.xoffset != 0 || var.yoffset != 0 ||
       var.bits_per_pixel != 32 || var.rotate != FB_ROTATE_UR ||
       var.vmode != FB_VMODE_NONINTERLACED ||
       var.red.offset != 16 || var.red.length != 8 || var.red.msb_right ||
       var.green.offset != 8 || var.green.length != 8 || var.green.msb_right ||
       var.blue.offset != 0 || var.blue.length != 8 || var.blue.msb_right ||
       (var.transp.length != 0 &&
        (var.transp.offset != 24 || var.transp.length != 8 || var.transp.msb_right)) ||
       fix.type != FB_TYPE_PACKED_PIXELS || fix.visual != FB_VISUAL_TRUECOLOR ||
       fix.line_length < engine->width * 4U ||
       (uint64_t)fix.line_length * var.yres > fix.smem_len) return 0;
    return 1;
}

PocketLvglEngine *pocket_engine_create_fbdev(uint32_t width, uint32_t height, const char *path) {
    if(width == 0 || height == 0 || !path) return NULL;
    PocketLvglEngine *engine = alloc_engine(width, height, 40);
    if(!engine) return NULL;
    if(!preflight_fbdev(engine, path)) goto fail_without_lvgl;

    lv_init();
    engine->display = lv_linux_fbdev_create();
    if(!engine->display) goto fail;
    /* Physical F0 must make the panel visible. Skipping FBIOBLANK unblank can
     * produce a successful render/flush on a blanked LCD, which is a false
     * acceptance. Let the pinned LVGL fbdev driver issue FB_BLANK_UNBLANK. */
    lv_linux_fbdev_set_skip_unblank(engine->display, false);
    if(lv_linux_fbdev_set_file(engine->display, path) != LV_RESULT_OK) goto fail;
    if(lv_display_get_horizontal_resolution(engine->display) != (int32_t)width ||
       lv_display_get_vertical_resolution(engine->display) != (int32_t)height ||
       lv_display_get_color_format(engine->display) != LV_COLOR_FORMAT_XRGB8888) goto fail;

    engine->delegate_flush = lv_display_get_flush_cb(engine->display);
    if(!engine->delegate_flush) goto fail;
    engine->framebuffer_active = 1;
    lv_display_set_user_data(engine->display, engine);
    lv_display_set_flush_cb(engine->display, forwarding_flush_cb);
    init_screen(engine);
    return engine;

fail:
    if(engine->display) lv_display_delete(engine->display);
    lv_deinit();
fail_without_lvgl:
    if(engine->lock_fd >= 0) close(engine->lock_fd);
    free(engine);
    return NULL;
}

void pocket_engine_destroy(PocketLvglEngine *engine) {
    if(!engine) return;
    if(engine->display) lv_display_delete(engine->display);
    free(engine->draw_buffer);
    if(engine->lock_fd >= 0) close(engine->lock_fd);
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
    const uint64_t start = cpu_ns();
    engine->metrics.bridge_update_calls++;
    if(engine->last_bar[id - 1] == value) {
        engine->metrics.bridge_duplicate_updates++;
        const uint64_t end = cpu_ns();
        if(end >= start) engine->metrics.bridge_update_cpu_ns += end - start;
        return 1;
    }
    engine->last_bar[id - 1] = value;
    lv_bar_set_value(obj, value, LV_ANIM_OFF);
    const uint64_t end = cpu_ns();
    if(end >= start) engine->metrics.bridge_update_cpu_ns += end - start;
    return 1;
}

int pocket_engine_set_label(PocketLvglEngine *engine, PocketNode id, const char *text) {
    lv_obj_t *obj = node(engine, id);
    if(!obj || !text) return 0;
    const uint64_t start = cpu_ns();
    engine->metrics.bridge_update_calls++;
    if(strncmp(engine->last_label[id - 1], text, sizeof(engine->last_label[id - 1])) == 0) {
        engine->metrics.bridge_duplicate_updates++;
        const uint64_t end = cpu_ns();
        if(end >= start) engine->metrics.bridge_update_cpu_ns += end - start;
        return 1;
    }
    strncpy(engine->last_label[id - 1], text, sizeof(engine->last_label[id - 1]) - 1);
    engine->last_label[id - 1][sizeof(engine->last_label[id - 1]) - 1] = 0;
    lv_label_set_text(obj, text);
    const uint64_t end = cpu_ns();
    if(end >= start) engine->metrics.bridge_update_cpu_ns += end - start;
    return 1;
}

uint32_t pocket_engine_pump(PocketLvglEngine *engine) {
    if(!engine) return POCKET_NO_TIMER_SLEEP_MS;
    const uint64_t flush_before = engine->metrics.flush_calls;
    const uint64_t start = cpu_ns();
    const uint32_t wait = lv_timer_handler();
    const uint64_t end = cpu_ns();
    if(end >= start) engine->metrics.handler_cpu_ns += end - start;
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
int pocket_engine_framebuffer_active(const PocketLvglEngine *engine) { return engine ? engine->framebuffer_active : 0; }

int pocket_engine_force_render(PocketLvglEngine *engine) {
    if(!engine || !engine->display) return 0;
    const uint64_t before = engine->metrics.flush_calls;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(engine->display);
    return engine->metrics.flush_calls > before;
}
