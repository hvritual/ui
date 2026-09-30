#ifndef COFFEE_LINUX_FBDEV_H
#define COFFEE_LINUX_FBDEV_H
#include "presenter.h"
#include <stdio.h>

typedef struct {
    int fd, opened, writable, observed, mapped;
    void *mapping;
    size_t mapping_length;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    FbLayout layout;
    const char *error;
    int system_errno, cleanup_errno;
    uint64_t presents, bytes_written, partial_presents;
} FbDevice;

/* Pass zero-initialized storage. Probe is O_RDONLY and never maps memory. */
int fbdev_open(FbDevice *device, const char *path, int writable);
int fbdev_present(void *context, const HostFrame *frame);
/* Requires a successful full present before non-NULL damage. Mode/ownership
 * revalidation remains per present. No PAN, VSync, driver or mode changes. */
int fbdev_present_region(void *context, const HostFrame *frame, const FbDamage *damage);
int fbdev_close(FbDevice *device); /* Idempotent; cleanup failure is observable. */
int fbdev_report(FILE *output, const char *path, const FbDevice *device);
#endif
