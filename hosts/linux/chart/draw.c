#include "draw.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Text payload accepts only canonical ASCII base64; never JSON, JS source,
 * floating-point, pointers, filesystem paths or device-control operations. */
static int b64(unsigned char c){
    if(c>='A'&&c<='Z')return (int)c-'A';
    if(c>='a'&&c<='z')return (int)c-'a'+26;
    if(c>='0'&&c<='9')return (int)c-'0'+52;
    if(c=='+')return 62;
    if(c=='/')return 63;
    return -1;
}
int pocket_chart_decode(PocketChartModel *out,unsigned mode,uint32_t color,
                        const char *text,size_t length) {
    if(!out||!text||mode<POCKET_CHART_LINE||mode>POCKET_CHART_BUBBLE||
       !length||length>(((POCKET_CHART_POINTS*5u+2u)/3u)*4u)||length%4u)
        return 0;
    uint8_t raw[POCKET_CHART_POINTS*5u];
    size_t count=0;
    for(size_t i=0;i<length;i+=4){
        int a=b64((unsigned char)text[i]),b=b64((unsigned char)text[i+1]);
        int c=text[i+2]=='='?-1:b64((unsigned char)text[i+2]);
        int d=text[i+3]=='='?-1:b64((unsigned char)text[i+3]);
        if(a<0||b<0||(c<0&&text[i+2]!='=')||(d<0&&text[i+3]!='=')||
           (c<0&&d>=0)||((c<0||d<0)&&i+4!=length))
            return 0;
        unsigned bits=(unsigned)(a<<18)|(unsigned)(b<<12)|
                      (unsigned)((c<0?0:c)<<6)|(unsigned)(d<0?0:d);
        size_t n=c<0?1u:d<0?2u:3u;
        /* Non-canonical trailing bits and padding are forbidden. */
        if((n==1u&&(b&15))||(n==2u&&(c&3))||count+n>sizeof(raw))return 0;
        raw[count++]=(uint8_t)(bits>>16);
        if(n>=2u)raw[count++]=(uint8_t)(bits>>8);
        if(n==3u)raw[count++]=(uint8_t)bits;
    }
    if(!count||count%5u)return 0;
    size_t points=count/5u;
    if(points>POCKET_CHART_POINTS)return 0;
    PocketChartPoint tmp[POCKET_CHART_POINTS];
    for(size_t i=0;i<points;i++){
        const uint8_t *p=raw+i*5u;
        unsigned x=(unsigned)p[0]|((unsigned)p[1]<<8u);
        unsigned y=(unsigned)p[2]|((unsigned)p[3]<<8u);
        if(x>10000u||y>10000u)return 0;
        tmp[i]=(PocketChartPoint){(uint16_t)x,(uint16_t)y,p[4]};
    }
    memcpy(out->points,tmp,points*sizeof(tmp[0]));
    out->mode=(uint8_t)mode;
    out->color=color|0xff000000u;
    out->count=(uint16_t)points;
    if(++out->revision==0)out->revision=1;
    return 1;
}
static void le32(uint8_t *bytes,uint32_t n){
    bytes[0]=(uint8_t)n;bytes[1]=(uint8_t)(n>>8);
    bytes[2]=(uint8_t)(n>>16);bytes[3]=(uint8_t)(n>>24);
}
typedef struct {uint8_t *pixels;int w,h;} Canvas;
static void pixel(Canvas *c,int x,int y,uint32_t rgba){
    if(x<0||y<0||x>=c->w||y>=c->h)return;
    uint8_t *p=c->pixels+((size_t)y*(size_t)c->w+(size_t)x)*4u;
    p[0]=(uint8_t)rgba;p[1]=(uint8_t)(rgba>>8u);
    p[2]=(uint8_t)(rgba>>16u);p[3]=(uint8_t)(rgba>>24u);
}
static void line(Canvas *c,int x0,int y0,int x1,int y1,uint32_t color){
    int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1;
    int err=dx-dy;
    for(unsigned budget=0;budget<POCKET_CHART_WIDTH+POCKET_CHART_HEIGHT+4u;budget++){
        pixel(c,x0,y0,color);
        if(x0==x1&&y0==y1)return;
        int twice=2*err;
        if(twice>-dy){err-=dy;x0+=sx;}
        if(twice<dx){err+=dx;y0+=sy;}
    }
}
static void disk(Canvas *c,int cx,int cy,int radius,uint32_t color){
    if(radius>20)radius=20;
    for(int y=-radius;y<=radius;y++)
        for(int x=-radius;x<=radius;x++)
            if(x*x+y*y<=radius*radius)pixel(c,cx+x,cy+y,color);
}
static void rect(Canvas *c,int left,int top,int right,int bottom,uint32_t color){
    if(left<0)left=0;if(top<0)top=0;
    if(right>=c->w)right=c->w-1;if(bottom>=c->h)bottom=c->h-1;
    for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++)pixel(c,x,y,color);
}
int pocket_chart_packet(const PocketChartModel *m,uint64_t id,uint8_t **out,size_t *length){
    if(out)*out=NULL;if(length)*length=0;
    if(!m||!out||!length||!id||id>9007199254740991ULL||
       !m->count||m->count>POCKET_CHART_POINTS||
       m->mode<POCKET_CHART_LINE||m->mode>POCKET_CHART_BUBBLE||
       m->width<64||m->height<64||m->width>POCKET_CHART_WIDTH||
       m->height>POCKET_CHART_HEIGHT)return 0;
    size_t pixels=(size_t)m->width*m->height*4u;
    if(pixels>POCKET_CHART_MAX_PACKET-POCKET_CHART_HEADER_BYTES)return 0;
    uint8_t *packet=malloc(POCKET_CHART_HEADER_BYTES+pixels);
    if(!packet)return 0;
    le32(packet,POCKET_CHART_WIRE_MAGIC);
    le32(packet+4,(uint32_t)id);
    le32(packet+8,(uint32_t)(id>>32));
    le32(packet+12,m->width);
    le32(packet+16,m->height);
    le32(packet+20,m->revision);
    le32(packet+24,(uint32_t)pixels);
    le32(packet+28,0);
    Canvas canvas={packet+POCKET_CHART_HEADER_BYTES,(int)m->width,(int)m->height};
    /* Every chart is opaque. Color state and drawn extent are deterministic. */
    rect(&canvas,0,0,canvas.w-1,canvas.h-1,0xfff7f8f8u);
    const int x0=32,y0=12,xmax=canvas.w-10,ymax=canvas.h-26;
    line(&canvas,x0,ymax,xmax,ymax,0xff858786u);
    line(&canvas,x0,ymax,x0,y0,0xff858786u);
    const int dx=xmax-x0,dy=ymax-y0;
    int prevx=0,prevy=0;
    for(unsigned i=0;i<m->count;i++){
        unsigned xx=m->points[i].x,yy=m->points[i].y;
        if(xx>10000u||yy>10000u){free(packet);return 0;}
        int px=x0+(int)((uint32_t)dx*xx/10000u);
        int py=ymax-(int)((uint32_t)dy*yy/10000u);
        switch(m->mode){
          case POCKET_CHART_LINE:
            if(i)line(&canvas,prevx,prevy,px,py,m->color);
            if(i==0||i+1==m->count)disk(&canvas,px,py,2,m->color);
            break;
          case POCKET_CHART_BAR:
            rect(&canvas,px-2,py,px+2,ymax-1,m->color);break;
          case POCKET_CHART_SCATTER:
            disk(&canvas,px,py,2,m->color);break;
          case POCKET_CHART_BUBBLE:{
            int radius=2+(int)m->points[i].size/18;
            disk(&canvas,px,py,radius,m->color);
            break;
          }
          default: free(packet);return 0;
        }
        prevx=px;prevy=py;
    }
    *out=packet;
    *length=POCKET_CHART_HEADER_BYTES+pixels;
    return 1;
}
