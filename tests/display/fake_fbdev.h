#ifndef TEST_FAKE_FBDEV_H
#define TEST_FAKE_FBDEV_H
#include "display/fbdev.h"

typedef struct {
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    int open_error, stat_error, regular_file, lock_error, query_error_at;
    int map_error, unmap_error, close_error, page_error;
    int opens, closes, maps, unmaps, queries, locks, forbidden;
    int last_flags;
    uint8_t *data;
} FakeFb;
extern FakeFb fake_fb;
void fake_fb_reset(uint32_t width, uint32_t height, unsigned bpp);
int fake_fb_guards(void);
#endif
