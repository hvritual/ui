#include "hosts/linux/framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"SCENE_WIRE_FAIL line=%d %s\n",__LINE__,#x);goto cleanup;}}while(0)
static void word(uint8_t *p,unsigned i,uint32_t v){p+=i*4;p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
/* Hand-built wire independent of the production serializer. Negative x and an
 * ID above 2^32 must be decoded without sign/precision loss. */
static void fixture(uint8_t *b){
    memset(b,0,192);word(b,0,0x31435350);word(b,1,1024);word(b,2,600);word(b,3,2);
    word(b,5,0xff302010);word(b,6,1);
    for(unsigned i=0;i<2;i++){
        unsigned o=8+i*20;word(b,o,i+1);word(b,o+1,0x200);
        word(b,o+2,1);word(b,o+3,i?30:(uint32_t)-10);word(b,o+4,10);
        word(b,o+5,20);word(b,o+6,20);word(b,o+9,1024);word(b,o+10,600);
        word(b,o+11,0xff665544);word(b,o+14,256);word(b,o+19,100);
    }
}
int test_scene_wire(const char *assets){
    int ok=0;PocketSceneEngine *e=calloc(1,sizeof(*e));uint8_t b[193],bad[193];
    PocketEngineFrame f;uint8_t *pixels=NULL;uint32_t next;
    REQUIRE(e);pocket_scene_engine_init(e,assets);
    REQUIRE(pocket_scene_engine_api.open(e,&(PocketEngineOpenConfig){1024,600,1})==POCKET_ENGINE_OK);
    fixture(b);REQUIRE(pocket_runtime_resource_pack(b,192)==1);
    REQUIRE(pocket_scene_engine_api.tick(e,1,&next)==POCKET_ENGINE_OK);
    REQUIRE(pocket_scene_engine_api.render(e,&f)==POCKET_ENGINE_OK);
    /* Header ABGR becomes opaque BGRA in the renderer, and x=-10 is clipped. */
    REQUIRE(f.pixels[0]==0x30&&f.pixels[1]==0x20&&f.pixels[2]==0x10);
    REQUIRE(f.pixels[15*f.stride+5*4]==0x66&&f.pixels[15*f.stride+5*4+2]==0x44);
    pixels=malloc(f.length);REQUIRE(pixels);memcpy(pixels,f.pixels,f.length);
    for(unsigned test=0;test<12;test++){
        memcpy(bad,b,192);size_t length=192;
        switch(test){
        case 0:length=20;break;
        case 1:length=191;break;
        case 2:length=193;bad[192]=0;break;
        case 3:word(bad,3,257);break;
        case 4:word(bad,7,1);break;
        case 5:word(bad,4,6);break;
        case 6:word(bad,6,2);break;
        case 7:word(bad,28,1);break; /* duplicate stable ID */
        case 8:word(bad,10,999);break; /* unknown component */
        case 9:word(bad,13,0xffffffffU);break; /* negative width */
        case 10:word(bad,9,0x200000);break; /* unsafe JS ID */
        default:word(bad,24,9);break; /* resource ref beyond atlas */
        }
        REQUIRE(pocket_runtime_resource_pack(bad,length)==-1);
        REQUIRE(pocket_scene_engine_api.tick(e,test+2,&next)==POCKET_ENGINE_OK);
        REQUIRE(pocket_scene_engine_api.render(e,&f)==POCKET_ENGINE_OK);
        REQUIRE(!memcmp(pixels,f.pixels,f.length));
    }
    ok=1;puts("SCENE_WIRE_OK hand-encoded high-id signed-geometry 12-negative immutable-old-frame");
cleanup:
    free(pixels);if(e&&e->opened)(void)pocket_scene_engine_api.close(e);free(e);return ok;
}
int test_scene_coalescing(const char *assets){
    int ok=0;PocketFramework *r=calloc(1,sizeof(*r));REQUIRE(r);
    REQUIRE(pocket_framework_open(r,600,8,assets,NULL,NULL));
    REQUIRE(pocket_framework_tick(r,1000000000ULL,1));
    for(unsigned i=1;i<=40;i++){
        uint64_t ns=1000000000ULL+(uint64_t)i*17000000ULL;
        InputFrame f={.sequence=i,.contact_count=1,.contacts={{0,800-(int)i*8,200}}};
        REQUIRE(pocket_framework_input(r,&f,ns));
        uint64_t uploads=r->engine.scene_uploads;int skipped=((r->ticks+1)%2)!=0;
        REQUIRE(pocket_framework_tick(r,ns,0));
        if(skipped)REQUIRE(r->engine.scene_uploads==uploads);
    }
    REQUIRE(r->ticks==41&&r->engine.scene_uploads<=21&&r->engine.scene_wire_bytes>0);
    REQUIRE(pocket_framework_tick(r,1800000000ULL,1));
    CoffeeAppStats stats;REQUIRE(coffee_app_stats(&r->app,&stats));
    REQUIRE(r->presented_scroll_x==stats.scroll_x&&stats.scroll_x>0);
    ok=1;puts("SCENE_COALESCING_OK all-input-kept ticks41 uploads-at-most21 latest-frame-visible");
cleanup:
    if(r){(void)pocket_framework_close(r);free(r);}return ok;
}
