#include "hosts/linux/engine/drawing_raster.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    PocketDrawSurface *s=calloc(1,sizeof(*s));assert(s);
    s->id=1;s->revision=1;s->width=8;s->height=8;s->color=0xff123456;s->count=2;
    s->points[0]=(PocketDrawPoint){0,0};s->points[1]=(PocketDrawPoint){7,7};
    uint8_t pixels[256];assert(pocket_draw_raster(s,pixels,sizeof(pixels)));
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
        const uint8_t *p=pixels+(y*8+x)*4;
        if(x==y)assert(p[0]==0x12&&p[1]==0x34&&p[2]==0x56&&p[3]==255);
        else assert(!p[0]&&!p[1]&&!p[2]&&!p[3]);
    }
    s->points[1]=(PocketDrawPoint){8,7};assert(!pocket_draw_valid(s));
    s->points[1]=(PocketDrawPoint){7,7};s->count=4097;assert(!pocket_draw_valid(s));
    s->count=2;s->width=1024;s->height=800;assert(!pocket_draw_valid(s));
    s->width=s->height=8;s->revision=0;assert(!pocket_draw_valid(s));
    s->revision=1;assert(!pocket_draw_raster(s,pixels,255));
    s->count=1024;
    for(unsigned i=0;i<s->count;i++)s->points[i]=(PocketDrawPoint){(uint16_t)(i%8),(uint16_t)((i/8)%8)};
    assert(pocket_draw_raster(s,pixels,sizeof(pixels)));
    for(unsigned i=0;i<64;i++)assert(pixels[i*4+3]==255);
    free(s);puts("DRAWING_RASTER_OK 1024-vertices bounded-negative-tests");return 0;
}
