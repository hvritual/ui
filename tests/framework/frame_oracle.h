#ifndef TEST_FRAME_ORACLE_H
#define TEST_FRAME_ORACLE_H
#include "hosts/linux/display/presenter.h"
#include "hosts/linux/engine/contract.h"
#include <stdlib.h>
#include <string.h>
/* This test target accumulates only reported damage in one buffer, then compares
 * every pixel (including untouched margins) with an independent full copy. */
typedef struct {
    uint8_t *actual,*expected;
    size_t length;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    uint64_t presents,partial,bytes;
} FrameOracle;
static int oracle_open(FrameOracle *o,unsigned width,unsigned height){
    memset(o,0,sizeof(*o));o->length=(size_t)width*height*4;
    o->actual=calloc(1,o->length);o->expected=calloc(1,o->length);
    o->fix.type=FB_TYPE_PACKED_PIXELS;o->fix.visual=FB_VISUAL_TRUECOLOR;
    o->fix.line_length=width*4;o->fix.smem_len=(uint32_t)o->length;
    o->var.xres=o->var.xres_virtual=width;o->var.yres=o->var.yres_virtual=height;
    o->var.bits_per_pixel=32;o->var.red=(struct fb_bitfield){16,8,0};
    o->var.green=(struct fb_bitfield){8,8,0};o->var.blue=(struct fb_bitfield){0,8,0};
    return o->actual&&o->expected;
}
static void oracle_close(FrameOracle *o){free(o->actual);free(o->expected);}
static PocketEngineStatus oracle_present(void *p,const PocketEngineFrame *f){
    FrameOracle *o=p;HostFrame frame={f->pixels,f->width,f->height,f->stride,f->length};
    const FbDamage d={f->damage.x,f->damage.y,f->damage.width,f->damage.height};
    const FbDamage *region=o->presents&&f->damage_valid?&d:NULL;
    if(fb_copy(&o->fix,&o->var,o->expected,o->length,&frame)||
       fb_copy_region(&o->fix,&o->var,o->actual,o->length,&frame,region)||
       memcmp(o->actual,o->expected,o->length))return POCKET_ENGINE_BACKEND_FAILED;
    o->presents++;o->bytes+=region?(uint64_t)region->width*(unsigned)region->height*4:o->length;
    if(region&&(uint64_t)region->width*(unsigned)region->height<f->width*f->height)o->partial++;
    return POCKET_ENGINE_OK;
}
#endif
