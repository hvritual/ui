#ifndef COFFEE_FRAMEBUFFER_PRESENTER_H
#define COFFEE_FRAMEBUFFER_PRESENTER_H
#include "../frame.h"
#include <linux/fb.h>

#define FB_MAP_LIMIT (64U * 1024U * 1024U)
typedef struct {
    uint32_t width, height, stride, bytes_per_pixel;
    size_t first_byte, end_byte;
    unsigned red, green, blue, alpha;
    int has_alpha, rgb565, pan_candidate;
} FbLayout;

typedef struct { int32_t x, y, width, height; } FbDamage;

/* NULL damage requests the full frame. Invalid/nonpositive/out-of-range damage
 * fails before any write; source/mapping/format checks are identical to fb_copy. */
const char *fb_copy_region(const struct fb_fix_screeninfo *fix,
                          const struct fb_var_screeninfo *var,
                          void *mapped, size_t mapped_length, const HostFrame *frame,
                          const FbDamage *damage);

/* NULL means success. No mode setting, allocation, clipping or scaling. */
const char *fb_layout(const struct fb_fix_screeninfo *fix,
                      const struct fb_var_screeninfo *var, FbLayout *out);
const char *fb_copy(const struct fb_fix_screeninfo *fix,
                    const struct fb_var_screeninfo *var,
                    void *mapped, size_t mapped_length, const HostFrame *frame);
#endif
