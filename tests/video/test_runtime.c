#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "input/bridge.h"
#include "media/store.h"
#include "video/standby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned hooks;
void pocket_bench_stage(int n){(void)n;hooks++;}
static int hit(void *ctx,float x,float y){(void)ctx;return pocket_runtime_hit_test_bounds(x,y);}
static int inspect(int op){int32_t n=-999;if(!pocket_runtime_harness_call(op,0,&n))return -999;return n;}
static int delivery(StandbyVideo *v,InputBridge *b,LinuxHost *h,InputFrame *f,uint64_t now) {
 VideoMode before=v->mode;
 if(standby_input(v,f,now)){if(!input_bridge_ingest(b,f,now*1000000))return 0;}
 else if(before!=VIDEO_HELD_EXIT&&v->mode==VIDEO_HELD_EXIT){if(!input_bridge_cancel_all(b,now*1000000))return 0;}
 PocketRuntimeContactsInput wire={0};uint64_t stamp;
 return input_bridge_next(b,&wire,&stamp)&&host_turn_contacts(h,&wire);
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"VIDEO_CORE_FAIL line=%d %s\n",__LINE__,#x);failed=1;goto done;}}while(0)
int main(int argc,char **argv) {
 if(argc<2)return 2;
 int failed=0;LinuxHost h={0};StandbyVideo v={0};InputBridge b={0};HostFrame frame;InputFrame input={0};
 CHECK(host_open(&h,"imx6ul-1024x600",argv[1],"coffee.js","labels.atlas",0));CHECK(media_builtin(argv[1]));
 CHECK(pocket_runtime_harness_bind("inspect"));CHECK(input_bridge_init(&b,hit,NULL));CHECK(standby_init(&v,"/not-used",0));
 CHECK(host_render(&h,&frame));CHECK(inspect(1)==0);
 v.mode=VIDEO_PLAYING;v.session=19;
 input.contact_count=1;input.contacts[0]=(InputContact){0,150,170};
 CHECK(delivery(&v,&b,&h,&input,100));CHECK(v.mode==VIDEO_HELD_EXIT&&inspect(1)==0);
 input.contacts[0].x=160;CHECK(delivery(&v,&b,&h,&input,110));CHECK(inspect(1)==0);
 input.contact_count=0;CHECK(delivery(&v,&b,&h,&input,120));CHECK(v.mode==VIDEO_BUSINESS&&inspect(1)==0);
 puts("PASS video-real-core-wake-down-move-up-no-drink-click");
 input.contact_count=1;CHECK(delivery(&v,&b,&h,&input,200));input.contact_count=0;CHECK(delivery(&v,&b,&h,&input,210));
 CHECK(inspect(1)==1);CHECK(pocket_runtime_resource_pack(NULL,0)==0);
 puts("PASS video-real-core-next-gesture-selects-modal-blocks-idle");
 input.contact_count=1;input.contacts[0]=(InputContact){0,330,470};CHECK(delivery(&v,&b,&h,&input,300));input.contact_count=0;CHECK(delivery(&v,&b,&h,&input,310));
 CHECK(inspect(1)==0);CHECK(host_render(&h,&frame));CHECK(hooks>0);
 uint8_t *canvas=calloc(1,frame.length),pixels[16*16*4],head[64]={0};CHECK(canvas);
 memcpy(head,"PUIVFR1\0",8);video_put32(head+8,1);video_put32(head+12,16);video_put32(head+16,16);video_put32(head+20,64);video_put32(head+24,1024);video_put32(head+28,1);video_put64(head+32,88);video_put64(head+40,1);
 for(unsigned i=0;i<sizeof(pixels);i+=4){pixels[i]=20;pixels[i+1]=90;pixels[i+2]=180;pixels[i+3]=255;}
 video_surface_reset(&v.surface,88);CHECK(video_surface_accept(&v.surface,head,pixels,sizeof(pixels)));
 memcpy(canvas,frame.pixels,frame.length);CHECK(video_surface_blit(&v.surface,canvas,frame.length,1024,600,4096,(VideoRect){0,0,1024,600}));
 CHECK(canvas[(300*1024+512)*4+2]==180);free(canvas);CHECK(host_render(&h,&frame));CHECK(inspect(1)==0);
 if(argc>2){FILE *out=fopen(argv[2],"wb");CHECK(out);fprintf(out,"P6\n1024 600\n255\n");for(size_t i=0;i<frame.length;i+=4){uint8_t rgb[]={frame.pixels[i+2],frame.pixels[i+1],frame.pixels[i]};if(fwrite(rgb,1,3,out)!=3){fclose(out);failed=1;goto done;}}CHECK(!fclose(out));}
 puts("PASS video-real-core-native-surface-and-home-restore");
 done:standby_close(&v);host_close(&h);if(host_alloc_stats().live_bytes)failed=1;
 if(failed)return 1;
 puts("VIDEO_CORE_OK cases=3 real_core=true physical_video_validated=false");return 0;
}
