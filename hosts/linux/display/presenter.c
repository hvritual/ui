#include "presenter.h"
#include <limits.h>
#include <string.h>

static int mul_size(size_t a, size_t b, size_t *out) {
    if (b && a > SIZE_MAX / b) return 0;
    *out = a * b; return 1;
}
static int add_size(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return 0;
    *out = a + b; return 1;
}
static int field8(struct fb_bitfield field) {
    return field.length == 8 && field.offset <= 24 &&
           field.offset % 8 == 0 && field.msb_right == 0;
}
const char *fb_layout(const struct fb_fix_screeninfo *f,
                      const struct fb_var_screeninfo *v, FbLayout *out) {
    const uint16_t endian = 1;
    size_t virtual_row, virtual_bytes, xbytes, ybytes, rows, rowbytes;
    FbLayout l = {0};
    if (!f || !v || !out) return "FB_ARGUMENT_INVALID";
    memset(out, 0, sizeof(*out));
    if (*(const uint8_t *)&endian != 1) return "FB_ENDIAN_UNSUPPORTED";
    if (f->type != FB_TYPE_PACKED_PIXELS || f->visual != FB_VISUAL_TRUECOLOR ||
        v->grayscale || v->nonstd) return "FB_VISUAL_UNSUPPORTED";
    if (v->rotate != FB_ROTATE_UR || v->vmode != FB_VMODE_NONINTERLACED)
        return "FB_ORIENTATION_UNSUPPORTED";
    if (v->bits_per_pixel == 32) {
        if (!field8(v->red) || !field8(v->green) || !field8(v->blue) ||
            v->red.offset == v->green.offset || v->red.offset == v->blue.offset ||
            v->green.offset == v->blue.offset) return "FB_BITFIELDS_UNSUPPORTED";
        if (v->transp.length && (!field8(v->transp) ||
            v->transp.offset == v->red.offset || v->transp.offset == v->green.offset ||
            v->transp.offset == v->blue.offset)) return "FB_BITFIELDS_UNSUPPORTED";
        if (v->transp.msb_right || (!v->transp.length && v->transp.offset > 32))
            return "FB_BITFIELDS_UNSUPPORTED";
        l.bytes_per_pixel = 4; l.red = v->red.offset; l.green = v->green.offset;
        l.blue = v->blue.offset; l.alpha = v->transp.offset; l.has_alpha = v->transp.length != 0;
    } else if (v->bits_per_pixel == 16) {
        if (v->red.offset != 11 || v->red.length != 5 || v->red.msb_right ||
            v->green.offset != 5 || v->green.length != 6 || v->green.msb_right ||
            v->blue.offset != 0 || v->blue.length != 5 || v->blue.msb_right ||
            v->transp.length || v->transp.msb_right) return "FB_BITFIELDS_UNSUPPORTED";
        l.bytes_per_pixel = 2; l.rgb565 = 1;
    } else return "FB_BPP_UNSUPPORTED";
    if (!v->xres || !v->yres || v->xres > v->xres_virtual || v->yres > v->yres_virtual ||
        v->xoffset > v->xres_virtual - v->xres || v->yoffset > v->yres_virtual - v->yres)
        return "FB_GEOMETRY_INVALID";
    if (!f->smem_len || f->smem_len > FB_MAP_LIMIT ||
        f->smem_start > ULONG_MAX - f->smem_len) return "FB_MEMORY_INVALID";
    if (!mul_size(v->xres_virtual, l.bytes_per_pixel, &virtual_row) ||
        virtual_row > f->line_length || !mul_size(v->yres_virtual, f->line_length, &virtual_bytes) ||
        virtual_bytes > f->smem_len) return "FB_STRIDE_OR_MEMORY_INVALID";
    if (!mul_size(v->xoffset, l.bytes_per_pixel, &xbytes) ||
        !mul_size(v->yoffset, f->line_length, &ybytes) || !add_size(ybytes, xbytes, &l.first_byte) ||
        !mul_size(v->yres - 1, f->line_length, &rows) ||
        !mul_size(v->xres, l.bytes_per_pixel, &rowbytes) ||
        !add_size(l.first_byte, rows, &l.end_byte) || !add_size(l.end_byte, rowbytes, &l.end_byte) ||
        l.end_byte > f->smem_len) return "FB_RANGE_INVALID";
    l.width = v->xres; l.height = v->yres; l.stride = f->line_length;
    /* A reported candidate is NOT proof that FBIOPAN_DISPLAY works. */
    l.pan_candidate = f->ypanstep && v->yres <= v->yres_virtual / 2 &&
                      v->yres % f->ypanstep == 0 && v->yoffset == 0;
    *out = l; return NULL;
}
const char *fb_copy(const struct fb_fix_screeninfo *fix,
                    const struct fb_var_screeninfo *var,
                    void *mapped, size_t mapped_length, const HostFrame *frame) {
    FbLayout l;
    size_t source_rows, source_width, source_end;
    const char *error = fb_layout(fix, var, &l);
    if (error) return error;
    if (!mapped || !frame || !frame->pixels) return "FB_ARGUMENT_INVALID";
    if (mapped_length < fix->smem_len) return "FB_MAPPING_SHORT";
    if (frame->width != l.width || frame->height != l.height) return "FB_FRAME_GEOMETRY_MISMATCH";
    if (!mul_size(frame->width, 4, &source_width) || frame->stride < source_width ||
        !mul_size(frame->height - 1, frame->stride, &source_rows) ||
        !add_size(source_rows, source_width, &source_end) || source_end > frame->length)
        return "FB_SOURCE_RANGE_INVALID";
    uintptr_t src = (uintptr_t)frame->pixels, dst = (uintptr_t)mapped;
    if (src > UINTPTR_MAX - source_end || dst > UINTPTR_MAX - mapped_length ||
        (src < dst + mapped_length && dst < src + source_end)) return "FB_BUFFER_ALIAS_OR_OVERFLOW";
    /* All fallible checks precede the first write. Padding and hidden pixels
       remain untouched. Byte stores also support unaligned ARM scanlines. */
    for (uint32_t y = 0; y < l.height; ++y) {
        const uint8_t *s = frame->pixels + (size_t)y * frame->stride;
        uint8_t *d = (uint8_t *)mapped + l.first_byte + (size_t)y * l.stride;
        for (uint32_t x = 0; x < l.width; ++x, s += 4, d += l.bytes_per_pixel) {
            uint32_t pixel;
            if (l.rgb565) pixel = ((uint32_t)(s[2] >> 3) << 11) | ((uint32_t)(s[1] >> 2) << 5) | (s[0] >> 3);
            else pixel = ((uint32_t)s[2] << l.red) | ((uint32_t)s[1] << l.green) |
                         ((uint32_t)s[0] << l.blue) | (l.has_alpha ? 255U << l.alpha : 0);
            for (uint32_t b = 0; b < l.bytes_per_pixel; ++b) d[b] = (uint8_t)(pixel >> (8 * b));
        }
    }
    return NULL;
}
