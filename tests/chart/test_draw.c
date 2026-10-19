#include "../../hosts/linux/chart/draw.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"CHART_ASSERT_LINE=%d\n",__LINE__);exit(2);}}while(0)
static const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static size_t encode(const uint8_t *src,size_t bytes,char *dest,size_t cap){
    size_t n=0;
    for(size_t i=0;i<bytes;i+=3){
        if(n+4>=cap)return 0;
        unsigned a=src[i],b=i+1<bytes?src[i+1]:0,c=i+2<bytes?src[i+2]:0;
        dest[n++]=alphabet[a>>2];
        dest[n++]=alphabet[((a&3u)<<4)|(b>>4)];
        dest[n++]=i+1<bytes?alphabet[((b&15u)<<2)|(c>>6)]:'=';
        dest[n++]=i+2<bytes?alphabet[c&63u]:'=';
    }
    dest[n]=0;return n;
}
static uint32_t u32(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--intentional-failure")){
        puts("CHART_INTENTIONAL_FAIL");return 1;
    }
    if(argc!=1)return 2;
    uint8_t points[POCKET_CHART_POINTS*5];
    for(unsigned i=0;i<POCKET_CHART_POINTS;i++){
        unsigned x=i*10000u/999u,y=(i*359u)%10001u;
        points[i*5]=(uint8_t)x;points[i*5+1]=(uint8_t)(x>>8);
        points[i*5+2]=(uint8_t)y;points[i*5+3]=(uint8_t)(y>>8);
        points[i*5+4]=(uint8_t)(i%256u);
    }
    char text[6700];
    size_t length=encode(points,sizeof(points),text,sizeof(text));
    CHECK(length==6668);
    uint8_t *prior=NULL;size_t prior_size=0;
    uint64_t id=UINT64_C(4294967333);
    PocketChartModel model={0};
    model.width=768;model.height=320;
    for(unsigned kind=1;kind<=4;kind++){
        CHECK(pocket_chart_decode(&model,kind,0xff9a7251u,text,length));
        CHECK(model.count==1000&&model.mode==kind&&model.points[999].x==10000);
        uint8_t *packet=NULL;size_t size=0;
        CHECK(pocket_chart_packet(&model,id,&packet,&size));
        CHECK(size==POCKET_CHART_HEADER_BYTES+768u*320u*4u);
        CHECK(u32(packet)==POCKET_CHART_WIRE_MAGIC);
        CHECK(u32(packet+4)==(uint32_t)id&&u32(packet+8)==1u);
        CHECK(u32(packet+12)==768&&u32(packet+16)==320);
        CHECK(u32(packet+20)==kind&&u32(packet+24)==768u*320u*4u);
        CHECK(!u32(packet+28));
        if(prior){CHECK(memcmp(prior,packet,size)!=0);free(prior);}
        prior=packet;prior_size=size;
    }
    uint8_t *repeat=NULL;size_t repeat_size=0;
    CHECK(pocket_chart_packet(&model,id,&repeat,&repeat_size));
    CHECK(repeat_size==prior_size&&!memcmp(repeat,prior,repeat_size));
    free(repeat);free(prior);
    CHECK(!pocket_chart_decode(&model,0,0,text,length));
    CHECK(!pocket_chart_decode(&model,5,0,text,length));
    CHECK(!pocket_chart_decode(&model,1,0,text,length-1));
    CHECK(!pocket_chart_decode(&model,1,0,"@AAA",4));
    CHECK(!pocket_chart_decode(&model,1,0,"AAAA====",8));
    CHECK(!pocket_chart_decode(&model,1,0,"AAAA",4)); /* decoded count not multiple of 5 */
    CHECK(model.count==1000); /* fail-closed update does not replace good state */
    uint8_t wrong[5]={0xff,0xff,0,0,0};
    char wrong_enc[16];
    size_t wrong_len=encode(wrong,sizeof(wrong),wrong_enc,sizeof(wrong_enc));
    CHECK(!pocket_chart_decode(&model,1,0,wrong_enc,wrong_len));
    CHECK(model.count==1000);
    uint8_t excess[(POCKET_CHART_POINTS+1u)*5u]={0};
    char too_large[6800];
    size_t excessive=encode(excess,sizeof(excess),too_large,sizeof(too_large));
    CHECK(!pocket_chart_decode(&model,1,0,too_large,excessive));
    model.width=769;
    uint8_t *bad=NULL;size_t bad_size=0;
    CHECK(!pocket_chart_packet(&model,id,&bad,&bad_size));
    CHECK(!bad&&!bad_size);
    model.width=768;
    CHECK(!pocket_chart_packet(&model,0,&bad,&bad_size));
    CHECK(!pocket_chart_packet(&model,UINT64_C(9007199254740992),&bad,&bad_size));
    puts("CHART_DRAW_OK four-series 1000 points; bounded deterministic raster; invalid rejected");
    return 0;
}
