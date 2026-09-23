#include "display/presenter.h"
#include <stdio.h>
#include <string.h>
/* Independent byte-addressed oracle: exercise 64-word boundaries, tails,
 * arbitrary source alpha, unaligned rows and untouched framebuffer padding. */
static int block_copy_cases(void) {
    const unsigned widths[]={1,63,64,65,127,128,129,257};
    unsigned char srcbuf[8192],dstbuf[8192],expected[8192];
    for(unsigned z=0;z<sizeof(widths)/sizeof(widths[0]);z++)
    for(unsigned alpha=0;alpha<2;alpha++)for(unsigned pad=0;pad<4;pad++) {
        unsigned w=widths[z],h=3,stride=w*4+pad+16,source_stride=w*4+pad;
        struct fb_fix_screeninfo fix={0};struct fb_var_screeninfo var={0};
        fix.type=FB_TYPE_PACKED_PIXELS;fix.visual=FB_VISUAL_TRUECOLOR;
        fix.line_length=stride;fix.smem_len=stride*5;
        var.xres=w;var.yres=h;var.xres_virtual=w+2;var.yres_virtual=5;
        var.xoffset=1;var.yoffset=1;var.bits_per_pixel=32;
        var.red=(struct fb_bitfield){16,8,0};var.green=(struct fb_bitfield){8,8,0};
        var.blue=(struct fb_bitfield){0,8,0};if(alpha)var.transp=(struct fb_bitfield){24,8,0};
        for(unsigned i=0;i<sizeof(srcbuf);i++)srcbuf[i]=(unsigned char)(i*13+z);
        memset(dstbuf,0xa5,sizeof(dstbuf));memcpy(expected,dstbuf,sizeof(dstbuf));
        HostFrame frame={srcbuf+1,w,h,source_stride,(h-1)*source_stride+w*4};
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            unsigned char *d=expected+stride*(y+1)+(x+1)*4;
            const unsigned char *p=frame.pixels+y*source_stride+x*4;
            d[0]=p[0];d[1]=p[1];d[2]=p[2];d[3]=alpha?255:0;
        }
        if(fb_copy(&fix,&var,dstbuf,fix.smem_len,&frame)||memcmp(dstbuf,expected,sizeof(dstbuf)))return 0;
    }
    return 1;
}
int main(void) {
    if(!block_copy_cases()) {fputs("BLOCK_COPY_FAILED\n",stderr);return 1;}
    puts("BLOCK_COPY_OK cases=64 bgrx-bgra-unaligned-oracle");return 0;
}
