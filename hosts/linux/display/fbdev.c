#define _POSIX_C_SOURCE 200809L
#include "fbdev.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int fail(FbDevice *d, const char *code, int number) {
    if (!d->error) { d->error = code; d->system_errno = number; }
    return 0;
}
static int read_info(int fd, struct fb_fix_screeninfo *f, struct fb_var_screeninfo *v) {
    memset(f, 0, sizeof(*f)); memset(v, 0, sizeof(*v));
    return ioctl(fd, FBIOGET_FSCREENINFO, f) == 0 && ioctl(fd, FBIOGET_VSCREENINFO, v) == 0;
}
int fbdev_close(FbDevice *d) {
    int ok = 1;
    if (!d) return 1;
    if (d->mapped) {
        if (munmap(d->mapping, d->mapping_length)) {
            d->cleanup_errno = errno; fail(d, "FB_UNMAP_FAILED", errno); ok = 0;
        } else { d->mapping = NULL; d->mapping_length = 0; d->mapped = 0; }
    }
    if (d->opened) {
        int fd = d->fd; d->opened = 0; d->fd = -1;
        /* On Linux close must not be retried after EINTR: fd may be reused. */
        if (close(fd)) { d->cleanup_errno = errno; fail(d, "FB_CLOSE_FAILED", errno); ok = 0; }
    }
    return ok;
}
int fbdev_open(FbDevice *d, const char *path, int writable) {
    struct stat st;
    long page;
    if (!d || !path || d->opened || d->mapped) return 0;
    memset(d, 0, sizeof(*d)); d->fd = -1; d->writable = writable != 0;
    d->fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (d->fd < 0) return fail(d, "FB_OPEN_FAILED", errno);
    d->opened = 1;
    if (fstat(d->fd, &st)) { fail(d, "FB_STAT_FAILED", errno); goto cleanup; }
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 29) { fail(d, "FB_NOT_FRAMEBUFFER", 0); goto cleanup; }
    if (writable && flock(d->fd, LOCK_EX | LOCK_NB)) { fail(d, "FB_LOCK_FAILED", errno); goto cleanup; }
    if (!read_info(d->fd, &d->fix, &d->var)) { fail(d, "FB_QUERY_FAILED", errno); goto cleanup; }
    d->observed = 1;
    d->error = fb_layout(&d->fix, &d->var, &d->layout);
    if (d->error) goto cleanup;
    page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || d->fix.smem_start % (unsigned long)page) {
        fail(d, "FB_UNALIGNED_MAPPING_UNSUPPORTED", 0); goto cleanup;
    }
    if (writable) {
        d->mapping_length = d->fix.smem_len;
        void *mapping = mmap(NULL, d->mapping_length, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, 0);
        if (mapping == MAP_FAILED) { d->mapping_length = 0; fail(d, "FB_MAP_FAILED", errno); goto cleanup; }
        d->mapping = mapping; d->mapped = 1;
        if (!mapping) { fail(d, "FB_NULL_MAPPING_UNSUPPORTED", 0); goto cleanup; }
    }
    return 1;
cleanup:
    fbdev_close(d); return 0;
}
int fbdev_present_region(void *context, const HostFrame *frame, const FbDamage *damage) {
    FbDevice *d = context;
    struct fb_fix_screeninfo f;
    struct fb_var_screeninfo v;
    FbLayout layout;
    if (!d || !d->opened || !d->writable || !d->mapped || d->error) return 0;
    if (!read_info(d->fd, &f, &v)) return fail(d, "FB_REQUERY_FAILED", errno);
    const char *error = fb_layout(&f, &v, &layout);
    if (error) return fail(d, error, 0);
    /* Do not keep writing through an old mapping after an observed mode change.
       Exclusive ownership remains an operator prerequisite; this is not KMS. */
    if (f.smem_start != d->fix.smem_start || f.smem_len != d->fix.smem_len ||
        v.xres_virtual != d->var.xres_virtual || v.yres_virtual != d->var.yres_virtual ||
        layout.width != d->layout.width || layout.height != d->layout.height ||
        layout.stride != d->layout.stride || layout.first_byte != d->layout.first_byte ||
        layout.end_byte != d->layout.end_byte || layout.bytes_per_pixel != d->layout.bytes_per_pixel ||
        layout.red != d->layout.red || layout.green != d->layout.green || layout.blue != d->layout.blue ||
        layout.alpha != d->layout.alpha || layout.has_alpha != d->layout.has_alpha ||
        layout.rgb565 != d->layout.rgb565) return fail(d, "FB_MODE_CHANGED", 0);
    if (damage && !d->presents) return fail(d,"FB_DAMAGE_WITHOUT_BASELINE",0);
    error = fb_copy_region(&f, &v, d->mapping, d->mapping_length, frame, damage);
    if (error) return fail(d, error, 0);
    ++d->presents;
    if(damage&&(uint64_t)damage->width*(uint32_t)damage->height<(uint64_t)layout.width*layout.height)++d->partial_presents;
    d->bytes_written += damage ? (uint64_t)damage->width * (uint32_t)damage->height * layout.bytes_per_pixel :
                                (uint64_t)layout.width * layout.height * layout.bytes_per_pixel;
    return 1;
}
int fbdev_present(void *context, const HostFrame *frame) {
    return fbdev_present_region(context,frame,NULL);
}
static void json_string(FILE *out, const char *text, size_t limit) {
    fputc('"', out);
    for (size_t i = 0; text && i < limit && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
        else if (c < 32 || c >= 127) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}
static void bitfield(FILE *out, const char *name, struct fb_bitfield field) {
    fprintf(out, "\"%s\":{\"offset\":%u,\"length\":%u,\"msb_right\":%u}",
            name, field.offset, field.length, field.msb_right);
}
int fbdev_report(FILE *out, const char *path, const FbDevice *d) {
    if (!out || !d) return 0;
    fprintf(out, "{\"schema_version\":1,\"device\":"); json_string(out, path, 4096);
    fprintf(out, ",\"observed\":%s,\"admitted\":%s,\"operation\":\"%s\",\"error\":",
            d->observed ? "true" : "false", d->observed && !d->error ? "true" : "false",
            d->writable ? "display-test" : "read-only-probe");
    if (d->error) json_string(out, d->error, 128); else fputs("null", out);
    fprintf(out, ",\"errno\":%d,\"cleanup_errno\":%d,\"presents\":%llu,",
            d->system_errno, d->cleanup_errno, (unsigned long long)d->presents);
    fputs("\"physical_panel_validated\":false,\"mode_changed_by_host\":false,\"info\":", out);
    if (!d->observed) fputs("null", out);
    else {
        const struct fb_fix_screeninfo *f = &d->fix;
        const struct fb_var_screeninfo *v = &d->var;
        fputs("{\"id\":", out); json_string(out, f->id, sizeof(f->id));
        fprintf(out, ",\"type\":%u,\"visual\":%u,\"line_length\":%u,\"smem_len\":%u,"
                "\"smem_start\":%lu,\"xres\":%u,\"yres\":%u,\"xres_virtual\":%u,\"yres_virtual\":%u,"
                "\"xoffset\":%u,\"yoffset\":%u,\"bits_per_pixel\":%u,\"rotate\":%u,\"vmode\":%u,"
                "\"grayscale\":%u,\"nonstd\":%u,\"width_mm\":%u,\"height_mm\":%u,",
                f->type, f->visual, f->line_length, f->smem_len, f->smem_start,
                v->xres, v->yres, v->xres_virtual, v->yres_virtual, v->xoffset, v->yoffset,
                v->bits_per_pixel, v->rotate, v->vmode, v->grayscale, v->nonstd, v->width, v->height);
        bitfield(out, "red", v->red); fputc(',', out); bitfield(out, "green", v->green);
        fputc(',', out); bitfield(out, "blue", v->blue); fputc(',', out); bitfield(out, "transp", v->transp);
        fprintf(out, ",\"xpanstep\":%u,\"ypanstep\":%u,\"ywrapstep\":%u,\"capabilities\":%u}",
                f->xpanstep, f->ypanstep, f->ywrapstep, f->capabilities);
    }
    fprintf(out, ",\"presentation\":{\"method\":\"mmap-row-copy\",\"pan_candidate\":%s,"
            "\"pan_verified\":false,\"pan_enabled\":false,\"vsync\":\"not-probed\","
            "\"vsync_enabled\":false,\"tearing_risk\":true,\"logical_scale\":1,\"dpi\":null}}\n",
            d->observed && !d->error && d->layout.pan_candidate ? "true" : "false");
    return !ferror(out);
}
