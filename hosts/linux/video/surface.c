#include "surface.h"
#include <stdlib.h>
#include <string.h>
int video_surface_init(VideoSurface *s) {
 if(!s)return 0;
 memset(s,0,sizeof(*s));s->pixels=malloc(VIDEO_FRAME_BYTES);return s->pixels!=NULL;
}
void video_surface_reset(VideoSurface *s,uint64_t session) {
 if(!s)return;
 s->valid=s->dirty=0;s->session=session;s->sequence=0;s->pts_us=0;
}
int video_surface_accept(VideoSurface *s,const uint8_t h[VIDEO_WIRE_BYTES],const uint8_t *p,size_t n) {
 if(!s||!s->pixels||!h||!p||memcmp(h,"PUIVFR1\0",8)||video_u32(h+8)!=1||video_u32(h+28)!=1)return 0;
 unsigned w=video_u32(h+12),height=video_u32(h+16),stride=video_u32(h+20);
 uint64_t session=video_u64(h+32),seq=video_u64(h+40),pts=video_u64(h+48);
 if(!video_dimensions(w,height)||stride!=w*4||n!=(size_t)stride*height||video_u32(h+24)!=n||
    session!=s->session||!session||seq==0||(s->valid&&(seq<=s->sequence||pts<s->pts_us))||pts>VIDEO_DURATION_US)return 0;
 memcpy(s->pixels,p,n);s->width=w;s->height=height;s->sequence=seq;s->pts_us=pts;s->valid=s->dirty=1;return 1;
}
int video_surface_blit(const VideoSurface *s,uint8_t *dst,size_t length,unsigned w,unsigned h,unsigned stride,VideoRect r) {
 if(!s||!s->valid||!s->pixels||!dst||!w||!h||w>4096||h>4096||stride<w*4||stride>SIZE_MAX/h||length<(size_t)stride*h||
    !r.width||!r.height||r.width>w||r.height>h||r.x>w-r.width||r.y>h-r.height)return 0;
 unsigned dw=r.width,dh=r.height;
 if((uint64_t)s->width*dh>(uint64_t)s->height*dw)dh=(unsigned)((uint64_t)s->height*dw/s->width);
 else dw=(unsigned)((uint64_t)s->width*dh/s->height);
 if(!dw)dw=1;
 if(!dh)dh=1;
 unsigned ox=r.x+(r.width-dw)/2,oy=r.y+(r.height-dh)/2;
 uint32_t xp[4096],xf[4096];
 for(unsigned x=0;x<dw;x++) {
  uint64_t q=dw>1?((uint64_t)x*(s->width-1)*65536)/(dw-1):0;
  xp[x]=(unsigned)(q>>16);xf[x]=(unsigned)(q&65535);
 }
 for(unsigned y=0;y<dh;y++) {
  uint64_t q=dh>1?((uint64_t)y*(s->height-1)*65536)/(dh-1):0;
  unsigned sy=(unsigned)(q>>16),fy=(unsigned)(q&65535),ny=sy+1<s->height?sy+1:sy;
  const uint8_t *a=s->pixels+(size_t)sy*s->width*4,*b=s->pixels+(size_t)ny*s->width*4;
  uint8_t *d=dst+(size_t)(oy+y)*stride+ox*4;
  for(unsigned x=0;x<dw;x++,d+=4) {
   unsigned sx=xp[x],nx=sx+1<s->width?sx+1:sx,fx=xf[x];
   for(unsigned c=0;c<3;c++) {
    uint32_t top=(a[sx*4+c]*(65536-fx)+a[nx*4+c]*fx+32768)>>16;
    uint32_t bottom=(b[sx*4+c]*(65536-fx)+b[nx*4+c]*fx+32768)>>16;
    d[c]=(uint8_t)((top*(65536-fy)+bottom*fy+32768)>>16);
   }
   d[3]=255;
  }
 }
 return 1;
}
void video_surface_close(VideoSurface *s) { if(s){free(s->pixels);memset(s,0,sizeof(*s));} }
