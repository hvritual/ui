#ifndef POCKET_ENGINE_DRAWING_RASTER_H
#define POCKET_ENGINE_DRAWING_RASTER_H
#include "drawing.h"
#include <stdlib.h>
#include <string.h>
/* Integer Bresenham stroke, one pixel wide, including every supplied point.
 * Maximum work is bounded by 4095 segments * 1024 pixels; no curve approximation
 * using component nodes. Transparent RGBA uploads use the existing real Core. */
static inline int pocket_draw_valid(const PocketDrawSurface *s) {
    if(!s||!s->id||s->id>9007199254740991ULL||!s->revision||!s->width||s->width>1024||
       !s->height||s->height>800||s->width*s->height>POCKET_DRAW_PIXELS||
       s->count<2||s->count>POCKET_DRAW_POINTS)return 0;
    for(uint32_t i=0;i<s->count;i++)if(s->points[i].x>=s->width||s->points[i].y>=s->height)return 0;
    return 1;
}
static inline int pocket_draw_raster(const PocketDrawSurface *s,uint8_t *rgba,size_t length) {
    if(!pocket_draw_valid(s)||!rgba||length!=(size_t)s->width*s->height*4U)return 0;
    memset(rgba,0,length);
    for(uint32_t i=1;i<s->count;i++) {
        int x=s->points[i-1].x,y=s->points[i-1].y;
        const int tx=s->points[i].x,ty=s->points[i].y;
        const int dx=abs(tx-x),dy=-abs(ty-y),sx=x<tx?1:-1,sy=y<ty?1:-1;
        int error=dx+dy;
        for(;;) {
            size_t at=((size_t)y*s->width+(unsigned)x)*4U;
            /* Theme/Core colors are packed ABGR; PSM_8888 byte order is RGBA. */
            rgba[at]=(uint8_t)s->color;rgba[at+1]=(uint8_t)(s->color>>8);
            rgba[at+2]=(uint8_t)(s->color>>16);rgba[at+3]=(uint8_t)(s->color>>24);
            if(x==tx&&y==ty)break;
            const int twice=2*error;
            if(twice>=dy){error+=dy;x+=sx;}
            if(twice<=dx){error+=dx;y+=sy;}
        }
    }
    return 1;
}
#endif
