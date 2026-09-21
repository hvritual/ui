#define _POSIX_C_SOURCE 200809L
#include "fake_fbdev.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int __real_open(const char *, int, ...);
int __real___open_2(const char *, int);
int __real_fstat(int, struct stat *);
int __real_flock(int, int);
int __real_ioctl(int, unsigned long, ...);
void *__real_mmap(void *, size_t, int, int, int, off_t);
int __real_munmap(void *, size_t);
int __real_close(int);
long __real_sysconf(int);
FakeFb fake_fb;
static uint8_t storage[8 * 1024 * 1024 + 32];
void fake_fb_reset(uint32_t w, uint32_t h, unsigned bpp) {
    memset(&fake_fb, 0, sizeof(fake_fb));
    fake_fb.fix.type = FB_TYPE_PACKED_PIXELS; fake_fb.fix.visual = FB_VISUAL_TRUECOLOR;
    fake_fb.fix.line_length = (w + 7) * (bpp / 8) + 3;
    fake_fb.fix.smem_len = fake_fb.fix.line_length * (h + 3);
    fake_fb.fix.smem_start = 4096;
    memcpy(fake_fb.fix.id, "fixture\"\\\n", 10);
    fake_fb.var.xres = w; fake_fb.var.yres = h;
    fake_fb.var.xres_virtual = w + 7; fake_fb.var.yres_virtual = h + 3;
    fake_fb.var.xoffset = 2; fake_fb.var.yoffset = 1; fake_fb.var.bits_per_pixel = bpp;
    fake_fb.var.red = (struct fb_bitfield){bpp == 32 ? 16 : 11, bpp == 32 ? 8 : 5, 0};
    fake_fb.var.green = (struct fb_bitfield){bpp == 32 ? 8 : 5, bpp == 32 ? 8 : 6, 0};
    fake_fb.var.blue = (struct fb_bitfield){0, bpp == 32 ? 8 : 5, 0};
    memset(storage, 0xa5, sizeof(storage)); fake_fb.data = storage + 17;
}
int fake_fb_guards(void) {
    if (fake_fb.fix.smem_len > sizeof(storage) - 32) return 0;
    for (unsigned i = 0; i < 17; i++) if (storage[i] != 0xa5) return 0;
    for (unsigned i = 0; i < 15; i++) if (fake_fb.data[fake_fb.fix.smem_len + i] != 0xa5) return 0;
    return 1;
}
int __wrap_open(const char *path, int flags, ...) {
    if (strcmp(path, "/dev/fb-fixture")) {
        if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode_t mode = (mode_t)va_arg(ap, int); va_end(ap); return __real_open(path, flags, mode); }
        return __real_open(path, flags);
    }
    fake_fb.opens++; fake_fb.last_flags = flags;
    if (fake_fb.open_error) { errno = fake_fb.open_error; return -1; }
    return 0; /* Exercise ownership of descriptor zero, not just positive fds. */
}
/* Fortified glibc lowers non-constant two-argument open to __open_2.
   Keep that protection in production and intercept only the fixture here. */
int __wrap___open_2(const char *path, int flags) {
    if (!strcmp(path, "/dev/fb-fixture")) return __wrap_open(path, flags);
    return __real___open_2(path, flags);
}
int __wrap_fstat(int fd, struct stat *st) {
    if (fd != 0) return __real_fstat(fd, st);
    if (fake_fb.stat_error) { errno = fake_fb.stat_error; return -1; }
    memset(st, 0, sizeof(*st)); st->st_mode = fake_fb.regular_file ? S_IFREG : S_IFCHR;
    st->st_rdev = makedev(29, 0); return 0;
}
int __wrap_flock(int fd, int operation) {
    if (fd != 0) return __real_flock(fd, operation);
    fake_fb.locks++;
    if (operation != (LOCK_EX | LOCK_NB)) fake_fb.forbidden++;
    if (fake_fb.lock_error) { errno = fake_fb.lock_error; return -1; }
    return 0;
}
int __wrap_ioctl(int fd, unsigned long request, ...) {
    (void)fd; va_list ap; va_start(ap, request); void *arg = va_arg(ap, void *); va_end(ap);
    if (fd != 0) return __real_ioctl(fd, request, arg);
    fake_fb.queries++;
    if (fake_fb.query_error_at == fake_fb.queries) { errno = EIO; return -1; }
    if (request == FBIOGET_FSCREENINFO) memcpy(arg, &fake_fb.fix, sizeof(fake_fb.fix));
    else if (request == FBIOGET_VSCREENINFO) memcpy(arg, &fake_fb.var, sizeof(fake_fb.var));
    else { fake_fb.forbidden++; errno = ENOTTY; return -1; }
    return 0;
}
void *__wrap_mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset) {
    if (fd != 0) return __real_mmap(address, length, prot, flags, fd, offset);
    fake_fb.maps++;
    if (address || length != fake_fb.fix.smem_len || prot != (PROT_READ | PROT_WRITE) ||
        flags != MAP_SHARED || offset || length > sizeof(storage)-32) {
        fake_fb.forbidden++; errno = EINVAL; return MAP_FAILED;
    }
    if (fake_fb.map_error) { errno = fake_fb.map_error; return MAP_FAILED; }
    return fake_fb.data;
}
int __wrap_munmap(void *address, size_t length) {
    if (address != fake_fb.data) return __real_munmap(address, length);
    fake_fb.unmaps++;
    if (address != fake_fb.data || length != fake_fb.fix.smem_len) fake_fb.forbidden++;
    if (fake_fb.unmap_error) { errno = fake_fb.unmap_error; return -1; }
    return 0;
}
int __wrap_close(int fd) {
    if (fd != 0) return __real_close(fd);
    fake_fb.closes++;
    if (fake_fb.close_error) { errno = fake_fb.close_error; return -1; }
    return 0;
}
long __wrap_sysconf(int name) {
    if (name != _SC_PAGESIZE) return __real_sysconf(name);
    return fake_fb.page_error ? -1 : 4096;
}
