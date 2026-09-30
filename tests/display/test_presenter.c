#define _POSIX_C_SOURCE 200809L
#include "fake_fbdev.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"DISPLAY_TEST_FAILED %d: %s\n",__LINE__,#x); failures++; goto cleanup; } } while (0)
#define PASS(x) puts("PASS " x)
static int unchanged(const uint8_t *p, size_t n) { for (size_t i=0;i<n;i++) if (p[i]!=0xa5) return 0; return 1; }
static int copy32(const struct fb_fix_screeninfo *f, const struct fb_var_screeninfo *v, const HostFrame *frame) {
    /* Oracle indexes bytes by channel, rather than duplicating integer packing. */
    uint8_t expected[2048];
    if (f->smem_len > sizeof(expected)) return 0;
    memset(expected, 0xa5, sizeof(expected));
    for (uint32_t y=0;y<v->yres;y++) for (uint32_t x=0;x<v->xres;x++) {
        size_t i=(y+v->yoffset)*f->line_length+(x+v->xoffset)*4;
        const uint8_t *s=frame->pixels+y*frame->stride+x*4;
        memset(expected+i,0,4);
        expected[i+v->red.offset/8]=s[2]; expected[i+v->green.offset/8]=s[1]; expected[i+v->blue.offset/8]=s[0];
        if (v->transp.length) expected[i+v->transp.offset/8]=255;
    }
    return !fb_copy(f,v,fake_fb.data,f->smem_len,frame) &&
           !memcmp(expected,fake_fb.data,f->smem_len) && fake_fb_guards();
}
#include "region_cases.h"
int main(int argc, char **argv) {
    FbDevice d={0}; FbLayout l;
    uint8_t pixels[64]; for (unsigned i=0;i<sizeof(pixels);i++) pixels[i]=(uint8_t)(i*13);
    HostFrame frame={pixels,3,2,16,28};
    CHECK(region_cases());
    int deliberate=argc>1 && !strcmp(argv[1],"--intentional-failure");
    fake_fb_reset(3,2,32);
    CHECK(copy32(&fake_fb.fix,&fake_fb.var,&frame));
    CHECK(fake_fb.data[0] == (deliberate ? 0 : 0xa5)); PASS("xrgb8888-padding-offset-canaries");
    for (unsigned r=0;r<4;r++) for (unsigned g=0;g<4;g++) for (unsigned b=0;b<4;b++) {
        if (r==g || r==b || g==b) continue;
        fake_fb_reset(3,2,32); fake_fb.var.red.offset=r*8; fake_fb.var.green.offset=g*8; fake_fb.var.blue.offset=b*8;
        CHECK(copy32(&fake_fb.fix,&fake_fb.var,&frame));
        fake_fb.var.transp=(struct fb_bitfield){(6-r-g-b)*8,8,0};
        CHECK(copy32(&fake_fb.fix,&fake_fb.var,&frame));
    }
    PASS("all-32bit-byte-orders-opaque-alpha");
    for (unsigned n=1;n<=64;n++) {
        fake_fb_reset(3,2,32); fake_fb.var.xoffset=n%5; fake_fb.var.yoffset=n%3;
        CHECK(copy32(&fake_fb.fix,&fake_fb.var,&frame));
    }
    PASS("offset-grid-preserves-hidden-pixels");
    fake_fb_reset(3,2,16);
    {
        uint8_t source[]={0,0,255,0, 0,255,0,0, 255,0,0,0, 255,255,255,0, 0,0,0,0, 0x56,0x34,0x12,0};
        uint16_t words[]={0xf800,0x07e0,0x001f,0xffff,0,0x11aa};
        HostFrame f={source,3,2,12,sizeof(source)};
        CHECK(!fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&f));
        CHECK(!fb_layout(&fake_fb.fix,&fake_fb.var,&l));
        for (unsigned y=0;y<2;y++) for (unsigned x=0;x<3;x++) {
            const uint8_t *p=fake_fb.data+l.first_byte+y*l.stride+x*2;
            CHECK(p[0]==(words[y*3+x]&255) && p[1]==(words[y*3+x]>>8));
        }
        CHECK(fake_fb_guards());
    }
    PASS("rgb565-exact-quantization");
#define REJECT(name,change) do { fake_fb_reset(3,2,32); change; CHECK(fb_layout(&fake_fb.fix,&fake_fb.var,&l)!=NULL); CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len<=2048?fake_fb.fix.smem_len:32)); PASS(name); } while(0)
    REJECT("reject-24bpp",fake_fb.var.bits_per_pixel=24);
    REJECT("reject-directcolor",fake_fb.fix.visual=FB_VISUAL_DIRECTCOLOR);
    REJECT("reject-planar",fake_fb.fix.type=FB_TYPE_PLANES);
    REJECT("reject-grayscale-fourcc",fake_fb.var.grayscale=1);
    REJECT("reject-nonstandard",fake_fb.var.nonstd=1);
    REJECT("reject-overlapping-channels",fake_fb.var.red.offset=8);
    REJECT("reject-msb-right",fake_fb.var.red.msb_right=1);
    REJECT("reject-nonbyte-channels",fake_fb.var.red.offset=15);
    REJECT("reject-overlapping-alpha",(fake_fb.var.transp=(struct fb_bitfield){8,8,0}));
    REJECT("reject-rotation",fake_fb.var.rotate=1);
    REJECT("reject-ywrap-interlace",fake_fb.var.vmode=FB_VMODE_YWRAP);
    REJECT("reject-zero-dimension",fake_fb.var.xres=0);
    REJECT("reject-offset-outside-virtual",fake_fb.var.xoffset=UINT_MAX);
    REJECT("reject-short-stride",fake_fb.fix.line_length=11);
    REJECT("reject-short-smem",fake_fb.fix.smem_len--);
    REJECT("reject-huge-map",fake_fb.fix.smem_len=UINT_MAX);
    REJECT("reject-virtual-overflow",fake_fb.var.yres_virtual=UINT_MAX);
    REJECT("reject-physical-address-overflow",fake_fb.fix.smem_start=ULONG_MAX-1);
#undef REJECT
    fake_fb_reset(3,2,32);
    CHECK(fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len-1,&frame));
    frame.length--; CHECK(fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&frame)); frame.length++;
    frame.stride=11; CHECK(fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&frame)); frame.stride=16;
    frame.width=2; CHECK(fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&frame)); frame.width=3;
    frame.pixels=fake_fb.data; CHECK(fb_copy(&fake_fb.fix,&fake_fb.var,fake_fb.data,fake_fb.fix.smem_len,&frame)); frame.pixels=pixels;
    CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len)); PASS("reject-source-map-alias-without-writes");
    fake_fb_reset(3,2,32); fake_fb.var.yoffset=0; fake_fb.var.yres_virtual=6; fake_fb.fix.smem_len=6*fake_fb.fix.line_length; fake_fb.fix.ypanstep=1;
    CHECK(!fb_layout(&fake_fb.fix,&fake_fb.var,&l) && l.pan_candidate);
    fake_fb.fix.ypanstep=0; CHECK(!fb_layout(&fake_fb.fix,&fake_fb.var,&l) && !l.pan_candidate);
    PASS("pan-is-only-a-reported-candidate");
    fake_fb_reset(3,2,32);
    CHECK(fbdev_open(&d,"/dev/fb-fixture",0)); CHECK(d.observed && !d.mapping && !fake_fb.maps && !fake_fb.locks);
    CHECK((fake_fb.last_flags&O_ACCMODE)==O_RDONLY); CHECK(!fbdev_present(&d,&frame));
    CHECK(fbdev_close(&d)); CHECK(fbdev_close(&d) && fake_fb.closes==1); PASS("read-only-probe-no-mapping-or-writes");
    fake_fb_reset(3,2,32);
    CHECK(fbdev_open(&d,"/dev/fb-fixture",1)); CHECK(d.fd==0 && fake_fb.maps==1 && fake_fb.locks==1);
    CHECK(fbdev_present(&d,&frame) && d.presents==1); CHECK(fbdev_close(&d));
    CHECK(fbdev_close(&d) && fake_fb.closes==1 && fake_fb.unmaps==1 && !fake_fb.forbidden); PASS("device-lifecycle-descriptor-zero");
#define OPEN_FAIL(name,change) do { fake_fb_reset(3,2,32); change; CHECK(!fbdev_open(&d,"/dev/fb-fixture",1)); CHECK(d.error && !d.opened && !d.mapping); CHECK(!fake_fb.forbidden); PASS(name); } while(0)
    OPEN_FAIL("open-permission-denied",fake_fb.open_error=EACCES);
    OPEN_FAIL("stat-failure-cleanup",fake_fb.stat_error=EIO);
    OPEN_FAIL("reject-regular-file-device",fake_fb.regular_file=1);
    OPEN_FAIL("exclusive-lock-failure",fake_fb.lock_error=EWOULDBLOCK);
    OPEN_FAIL("fixed-info-failure",fake_fb.query_error_at=1);
    OPEN_FAIL("variable-info-failure",fake_fb.query_error_at=2);
    OPEN_FAIL("mmap-failure-cleanup",fake_fb.map_error=ENOMEM);
    OPEN_FAIL("unaligned-smem-rejected",fake_fb.fix.smem_start=1);
    OPEN_FAIL("page-size-failure",fake_fb.page_error=1);
#undef OPEN_FAIL
    fake_fb_reset(3,2,32); CHECK(fbdev_open(&d,"/dev/fb-fixture",1)); fake_fb.var.xoffset++;
    CHECK(!fbdev_present(&d,&frame) && !strcmp(d.error,"FB_MODE_CHANGED"));
    CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len)); CHECK(fbdev_close(&d)); PASS("mode-change-fails-before-writing");
    fake_fb_reset(3,2,32); CHECK(fbdev_open(&d,"/dev/fb-fixture",1)); fake_fb.query_error_at=3;
    CHECK(!fbdev_present(&d,&frame)); CHECK(unchanged(fake_fb.data,fake_fb.fix.smem_len)); CHECK(fbdev_close(&d)); PASS("requery-error-latches");
    fake_fb_reset(3,2,32); CHECK(fbdev_open(&d,"/dev/fb-fixture",1)); fake_fb.unmap_error=EINVAL;
    CHECK(!fbdev_close(&d) && d.mapping && !d.opened && d.cleanup_errno==EINVAL);
    fake_fb.unmap_error=0; CHECK(fbdev_close(&d) && !d.mapping && fake_fb.closes==1); PASS("unmap-error-retains-owner-for-retry");
    fake_fb_reset(3,2,32); CHECK(fbdev_open(&d,"/dev/fb-fixture",1)); fake_fb.close_error=EINTR;
    CHECK(!fbdev_close(&d) && !d.opened); CHECK(fbdev_close(&d) && fake_fb.closes==1); PASS("close-eintr-not-retried");
    fake_fb_reset(3,2,32); CHECK(fbdev_open(&d,"/dev/fb-fixture",0)); CHECK(fbdev_close(&d));
    if (argc>1 && !deliberate) {
        FILE *f=fopen(argv[1],"w"); CHECK(f); int ok=fbdev_report(f,"/dev/fb\"\\\n",&d); int closed=fclose(f); CHECK(ok && !closed);
    }
    PASS("json-probe-escape-and-unknown-sync");
cleanup:
    fake_fb.unmap_error=0; fake_fb.close_error=0;
    if (!fbdev_close(&d)) failures++;
    if (failures) return 1;
    printf("PRESENTER_OK pointer_bits=%zu io=fixture physical_panel_validated=false\n",sizeof(void*)*8);
    return 0;
}
