#define _POSIX_C_SOURCE 200809L
#include "host.h"
#include "display/cli.h"
#include "fake_fbdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"DISPLAY_TEST_FAILED %d: %s\n",__LINE__,#x); failures++; goto cleanup; } } while (0)
static int rejects(void *context, const HostFrame *frame) { (void)context; (void)frame; return 0; }
static int matches(FbDevice *d, const char *assets, const char *output, unsigned turn, int deliberate) {
    char path[1024], header[80];
    size_t length=(size_t)d->layout.width*d->layout.height*3;
    uint8_t *actual=malloc(length), *expected=malloc(length);
    FILE *file=NULL; int ok=0;
    if (!actual || !expected) goto done;
    for (uint32_t y=0;y<d->layout.height;y++) for (uint32_t x=0;x<d->layout.width;x++) {
        const uint8_t *p=fake_fb.data+d->layout.first_byte+y*d->layout.stride+x*d->layout.bytes_per_pixel;
        uint8_t *rgb=actual+((size_t)y*d->layout.width+x)*3;
        if (d->layout.rgb565) {
            unsigned word=p[0]|((unsigned)p[1]<<8), r=(word>>11)&31, g=(word>>5)&63, b=word&31;
            rgb[0]=(uint8_t)((r<<3)|(r>>2)); rgb[1]=(uint8_t)((g<<2)|(g>>4)); rgb[2]=(uint8_t)((b<<3)|(b>>2));
        } else { rgb[0]=p[2]; rgb[1]=p[1]; rgb[2]=p[0]; if (p[3]!=0) goto done; }
    }
    snprintf(header,sizeof(header),"P6\n%u %u\n255\n",d->layout.width,d->layout.height);
    snprintf(path,sizeof(path),"%s/observed-%u-%u-%u.ppm",output,d->layout.height,turn,d->var.bits_per_pixel);
    file=fopen(path,"wb"); if (!file) goto done;
    int wrote=fputs(header,file)>=0 && fwrite(actual,1,length,file)==length;
    int closed=fclose(file); file=NULL; if (!wrote || closed) goto done;
    snprintf(path,sizeof(path),"%s/expected-%u-%u.ppm",assets,d->layout.height,turn);
    file=fopen(path,"rb"); if (!file) goto done;
    char input_header[80]; size_t header_length=strlen(header);
    if (fread(input_header,1,header_length,file)!=header_length || memcmp(header,input_header,header_length) ||
        fread(expected,1,length,file)!=length || fgetc(file)!=EOF) goto done;
    closed=fclose(file); file=NULL; if (closed) goto done;
    if (d->layout.rgb565) for (size_t i=0;i<length;i++) {
        unsigned shift=i%3==1?2:3; unsigned bits=expected[i]>>shift;
        expected[i]=(uint8_t)((bits<<shift)|(bits>>(8-2*shift)));
    }
    if (deliberate) expected[0]^=1;
    if (memcmp(actual,expected,length)) {
        size_t different=0, first=0;
        for (size_t i=0;i<length;i++) if (actual[i]!=expected[i]) { if (!different) first=i; different++; }
        fprintf(stderr,"GOLDEN_MISMATCH height=%u bpp=%u turn=%u bytes=%zu first_pixel=(%zu,%zu) actual=%u expected=%u\n",
                d->layout.height,d->var.bits_per_pixel,turn,different,(first/3)%d->layout.width,(first/3)/d->layout.width,actual[first],expected[first]);
        goto done;
    }
    /* Every byte outside the visible rows, including padding, is a canary. */
    for (size_t i=0;i<d->mapping_length;i++) {
        size_t row=i/d->layout.stride, col=i%d->layout.stride;
        size_t left=(size_t)d->var.xoffset*d->layout.bytes_per_pixel;
        int visible=row>=d->var.yoffset && row-d->var.yoffset<d->layout.height &&
                    col>=left && col-left<(size_t)d->layout.width*d->layout.bytes_per_pixel;
        if (!visible && fake_fb.data[i]!=0xa5) goto done;
    }
    ok=fake_fb_guards();
done:
    if (file) fclose(file);
    free(actual); free(expected); return ok;
}
int main(int argc, char **argv) {
    LinuxHost host={0}; HostFrame frame; FbDevice device={0};
    const char *assets=argc>1?argv[1]:"fixtures", *out=argc>2?argv[2]:".";
    int deliberate=argc>3 && !strcmp(argv[3],"--intentional-failure");
    char report[1024];
    for (unsigned h=600;h<=800;h+=200) for (unsigned bpp=16;bpp<=32;bpp+=16) {
        const char *profile=h==600?"imx6ul-1024x600":"imx6ul-1024x800";
        fake_fb_reset(1024,h,bpp);
        CHECK(host_open(&host,profile,assets,"display-scene.js","display-font.bin",0));
        CHECK(fbdev_open(&device,"/dev/fb-fixture",1)); CHECK(host_render(&host,&frame));
        CHECK(fbdev_present(&device,&frame)); CHECK(matches(&device,assets,out,0,deliberate));
        printf("PASS core-card-%u-%u-initial\n",h,bpp);
        CHECK(host_pump_present(&host,16666667,fbdev_present,&device)==1 && device.presents==1);
        CHECK(host_pump_present(&host,33333334,fbdev_present,&device)==1 && device.presents==2);
        CHECK(matches(&device,assets,out,2,0)); printf("PASS core-card-%u-%u-updated\n",h,bpp);
        CHECK(host_pause(&host,1,33333334)); CHECK(host_pump_present(&host,1000000000,fbdev_present,&device)==0);
        CHECK(device.presents==2); CHECK(host_pause(&host,0,1000000000));
        CHECK(host_pump_present(&host,1016666667,fbdev_present,&device)==1 && device.presents==2);
        CHECK(host_pump_present(&host,1033333334,fbdev_present,&device)==1 && device.presents==3);
        printf("PASS present-clock-pause-%u-%u\n",h,bpp);
        CHECK(fbdev_close(&device)); host_close(&host);
        CHECK(host_alloc_stats().live_bytes==0 && !fake_fb.forbidden);
    }
    fake_fb_reset(1024,600,32);
    CHECK(host_open(&host,"imx6ul-1024x600",assets,"display-scene.js","display-font.bin",0));
    CHECK(host_pump_present(&host,33333334,rejects,NULL)==-1 && host.state==HOST_FAILED);
    CHECK(!strcmp(host.error,"HOST_PRESENT_FAILED")); host_close(&host); puts("PASS callback-failure-latches-host");
    fake_fb_reset(1024,800,32);
    CHECK(host_open(&host,"imx6ul-1024x600",assets,"display-scene.js","display-font.bin",0));
    CHECK(fbdev_open(&device,"/dev/fb-fixture",1)); CHECK(host_render(&host,&frame));
    CHECK(!fbdev_present(&device,&frame) && device.presents==0);
    CHECK(!strcmp(device.error,"FB_FRAME_GEOMETRY_MISMATCH"));
    CHECK(fbdev_close(&device)); host_close(&host); puts("PASS wrong-panel-profile-no-scaling");
    fake_fb_reset(1024,600,32); snprintf(report,sizeof(report),"%s/cli-probe.json",out);
    {
        char *args[]={"ui-host","--probe-display","--fbdev","/dev/fb-fixture","--output",report};
        CHECK(display_cli(6,args)==0 && fake_fb.maps==0 && fake_fb.closes==1 && !fake_fb.forbidden);
        CHECK(display_cli(6,args)==1 && fake_fb.opens==1); /* Existing evidence must not be overwritten. */
    }
    puts("PASS cli-read-only-probe-exclusive-report");
    fake_fb_reset(1024,800,32); snprintf(report,sizeof(report),"%s/cli-display.json",out);
    {
        char *args[]={"ui-host","--display-test","--profile","imx6ul-1024x800","--fbdev","/dev/fb-fixture",
                      "--asset-root",(char *)assets,"--ticks","2","--output",report};
        CHECK(display_cli(12,args)==0 && fake_fb.maps==1 && fake_fb.unmaps==1 && !fake_fb.forbidden);
    }
    puts("PASS cli-display-test-real-core");
    {
        char *args[]={"ui-host","--display-test","--profile","imx6ul-1024x600","--ticks",""};
        CHECK(display_cli(6,args)==2); args[5]="-1"; CHECK(display_cli(6,args)==2);
        args[5]="601"; CHECK(display_cli(6,args)==2);
        char *probe[]={"ui-host","--probe-display","--profile","imx6ul-1024x600"}; CHECK(display_cli(4,probe)==2);
    }
    puts("PASS cli-reject-invalid-options");
    CHECK(host_alloc_stats().live_bytes==0); puts("PASS real-core-display-cleanup");
cleanup:
    if (!fbdev_close(&device)) failures++;
    host_close(&host);
    if (failures) return 1;
    printf("DISPLAY_CORE_OK pointer_bits=%zu renderer=real-pocketjs io=fixture physical_panel_validated=false\n",sizeof(void*)*8);
    return 0;
}
