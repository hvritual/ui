#define _POSIX_C_SOURCE 200809L
#include "display/vsync.h"
#include <errno.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int failures;
static int mode;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CHECK_FAILED line=%d %s\n",__LINE__,#x); failures++; } } while (0)
#define PASS(name) do { if (!failures) printf("PASS %s\n",name); } while (0)

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    if (request != FBIO_WAITFORVSYNC) return __real_ioctl(fd, request, NULL);
    switch (mode) {
        case 0: return 0;
        case 1: errno = ENOTTY; return -1;
        case 2: errno = EINVAL; return -1;
        case 3: errno = EINTR; return -1;
        case 4: for (;;) pause();
        default: errno = EIO; return -1;
    }
}

static void supported(void) {
    VsyncProbe p;
    mode=0; CHECK(vsync_probe_fd(7,5,50,&p));
    CHECK(p.status==VSYNC_STATUS_SUPPORTED && p.requested==5 && p.completed==5 && p.last_errno==0);
    PASS("vsync-supported");
}
static void unsupported_enotty(void) {
    VsyncProbe p;
    mode=1; CHECK(vsync_probe_fd(7,5,50,&p));
    CHECK(p.status==VSYNC_STATUS_UNSUPPORTED && p.completed==0 && p.last_errno==ENOTTY);
    PASS("vsync-enotty-unsupported");
}
static void unsupported_einval(void) {
    VsyncProbe p;
    mode=2; CHECK(vsync_probe_fd(7,5,50,&p));
    CHECK(p.status==VSYNC_STATUS_UNSUPPORTED && p.completed==0 && p.last_errno==EINVAL);
    PASS("vsync-einval-unsupported");
}
static void interrupted(void) {
    VsyncProbe p;
    mode=3; CHECK(vsync_probe_fd(7,5,50,&p));
    CHECK(p.status==VSYNC_STATUS_INTERRUPTED && p.completed==0 && p.last_errno==EINTR);
    PASS("vsync-eintr-classified");
}
static void timeout_case(void) {
    VsyncProbe p;
    mode=4; CHECK(vsync_probe_fd(7,2,20,&p));
    CHECK(p.status==VSYNC_STATUS_TIMEOUT && p.completed==0 && p.last_errno==ETIMEDOUT);
    PASS("vsync-timeout-bounded");
}
static void error_case(void) {
    VsyncProbe p;
    mode=5; CHECK(vsync_probe_fd(7,2,50,&p));
    CHECK(p.status==VSYNC_STATUS_ERROR && p.completed==0 && p.last_errno==EIO);
    PASS("vsync-error-classified");
}
static void argument_bounds(void) {
    VsyncProbe p;
    CHECK(!vsync_probe_fd(7,0,50,&p));
    CHECK(!vsync_probe_fd(7,121,50,&p));
    CHECK(!vsync_probe_fd(7,1,0,&p));
    CHECK(!vsync_probe_fd(7,1,1001,&p));
    PASS("vsync-argument-bounds");
}
static void report_json(void) {
    VsyncProbe p={0};
    p.status=VSYNC_STATUS_SUPPORTED; p.requested=3; p.completed=3; p.timeout_ms=100;
    p.samples_ns[0]=300; p.samples_ns[1]=100; p.samples_ns[2]=200;
    FILE *f=tmpfile(); CHECK(f && vsync_report(f,"/dev/fb0",&p));
    fflush(f); rewind(f); char buf[2048]={0}; size_t n=fread(buf,1,sizeof(buf)-1,f); (void)n; fclose(f);
    CHECK(strstr(buf,"\"status\":\"supported\"")!=NULL);
    CHECK(strstr(buf,"\"min\":100")!=NULL && strstr(buf,"\"median\":200")!=NULL && strstr(buf,"\"p95\":300")!=NULL && strstr(buf,"\"max\":300")!=NULL);
    CHECK(strstr(buf,"\"writes_framebuffer\":false")!=NULL && strstr(buf,"\"mode_changed_by_host\":false")!=NULL);
    PASS("vsync-json-stats");
}
int main(void) {
    supported(); unsupported_enotty(); unsupported_einval(); interrupted(); timeout_case(); error_case(); argument_bounds(); report_json();
    if (failures) { fprintf(stderr,"VSYNC_TEST_FAILED failures=%d\n",failures); return 1; }
    printf("VSYNC_TEST_OK cases=8 physical_panel_validated=false\n"); return 0;
}
