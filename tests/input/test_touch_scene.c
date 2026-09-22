#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include <stdio.h>
#include <string.h>
static int failures;
void pocket_bench_stage(int stage) { (void)stage; }
#define CHECK(x) do { if(!(x)){fprintf(stderr,"TOUCH_SCENE_FAILED line=%d %s\n",__LINE__,#x);failures++;goto done;} }while(0)
static int inspect(int op){int32_t v=-1;if(!pocket_runtime_harness_call(op,0,&v))failures++;return v;}
static int step(LinuxHost *h,int down,int x,int y,int hit,int cancel){
 PocketRuntimeContactsInput c={0};if(down){c.contact_count=1;c.contacts[0]=(PocketRuntimeContact){.id=0,.x=x,.y=y,.hit=hit};}
 if(cancel){c.cancelled_count=1;c.cancelled[0]=0;}return host_turn_contacts(h,&c);
}
int main(int argc,char **argv){
 LinuxHost h={0};HostFrame f;int hit=0;
 CHECK(argc==2);CHECK(host_open(&h,"imx6ul-1024x600",argv[1],"touch-scene.js","display-font.bin",0));
 CHECK(pocket_runtime_harness_bind("inspect"));CHECK(host_render(&h,&f));
 hit=pocket_runtime_hit_test_bounds(850,90);CHECK(hit==inspect(11));
 CHECK(step(&h,1,850,90,hit,0));for(int i=0;i<40;i++)CHECK(step(&h,1,850,90,hit,0));
 CHECK(inspect(1)==0&&inspect(4)==1);CHECK(step(&h,0,0,0,0,0));CHECK(inspect(1)==1);
 CHECK(pocket_runtime_action_value()==1);puts("PASS touch-asymmetric-target-long-press-release-once");
 hit=inspect(10);CHECK(step(&h,1,70,90,hit,0));CHECK(step(&h,1,850,90,hit,0));CHECK(step(&h,0,0,0,0,0));
 CHECK(inspect(1)==1&&inspect(2)==1);puts("PASS touch-drag-across-targets-does-not-click");
 hit=inspect(13);CHECK(step(&h,1,70,425,hit,0));CHECK(step(&h,0,0,0,0,1));
 CHECK(inspect(1)==1&&inspect(3)==1&&inspect(4)==0);puts("PASS touch-terminal-cancel-never-commits-click");
 CHECK(host_render(&h,&f));
 if(argc==2){char path[1024];int n=snprintf(path,sizeof(path),"%s/touch-preview.ppm",argv[1]);CHECK(n>0&&(size_t)n<sizeof(path));
   FILE *out=fopen(path,"wb");CHECK(out);fprintf(out,"P6\n%u %u\n255\n",f.width,f.height);
   int error=0;for(unsigned y=0;y<f.height;y++)for(unsigned x=0;x<f.width;x++){
     const unsigned char *v=f.pixels+y*f.stride+x*4;unsigned char rgb[3]={v[2],v[1],v[0]};if(fwrite(rgb,1,3,out)!=3)error=1;
   }if(fclose(out))error=1;CHECK(!error);
 }
 done:host_close(&h);if(host_alloc_stats().live_bytes)failures++;
 if(failures)return 1;
 puts("TOUCH_SCENE_OK cases=3 real_core=true");
 return 0;
}
