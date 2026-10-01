#include "input_font.h"
#include "../package/package.h"
#include <string.h>
static uint16_t u16(const unsigned char *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int pocket_input_font_valid(const void *data,size_t bytes){
    const unsigned char *b=data;
    if(!b||bytes<16||bytes>POCKET_INPUT_FONT_MAX||u32(b)!=0x41464344u||u16(b+4)!=3||
       !u16(b+6)||u16(b+6)>17000||b[8]!=24||b[9]!=30||b[10]!=24||b[11]!=36||
       b[12]!=1||b[13]||b[14]!=1||b[15])return 0;
    unsigned count=u16(b+6);
    if(bytes!=16+(size_t)count*(8+24*30))return 0;
    uint32_t previous=0;
    for(unsigned i=0;i<count;i++){
        const unsigned char *r=b+16+i*8;uint32_t c=u32(r);
        if(c<32||c>65535||(c>=0xd800&&c<=0xdfff)||(i&&c<=previous)||u16(r+4)>=count||r[6]!=24||r[7])return 0;
        previous=c;
    }
    /* The entire locked dictionary repertoire plus printable ASCII is required.
     * A hand-picked test subset cannot advertise the Pinyin input capability. */
    static const unsigned char inventory[32]={0xf2,0x6e,0xf3,0xb5,0xea,0xbc,0x39,0x9d,0xe5,0x34,0xe4,0xd8,0x25,0xba,0xfe,0x1c,0x96,0x86,0xab,0x47,0x24,0x0f,0x9c,0x2f,0x05,0x5b,0xbb,0xce,0x96,0x83,0x28,0x8d};
    unsigned char digest[32];pui_sha256(b+16,(size_t)count*8,digest);
    return !memcmp(digest,inventory,sizeof(digest));
}
int pocket_input_font_has(const void *data,size_t bytes,uint32_t c){
    const unsigned char *b=data;
    if(!b||bytes<16||bytes>POCKET_INPUT_FONT_MAX)return 0;
    size_t count=u16(b+6),lo=0,hi=count;
    if(!count||count>17000||bytes!=16+count*(8+24*30))return 0;
    while(lo<hi){size_t m=lo+(hi-lo)/2;uint32_t at=u32(b+16+m*8);if(at<c)lo=m+1;else hi=m;}
    return lo<count&&u32(b+16+lo*8)==c;
}
