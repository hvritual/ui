#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "media/store.h"
#include "pocket_ui_cabi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures; static unsigned stage_calls;
void pocket_bench_stage(int stage){(void)stage;++stage_calls;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"COFFEE_FAIL line=%d %s runtime=%s\n",__LINE__,#x,pocket_runtime_error());failures++;goto cleanup;}}while(0)
static int inspect(int op){int32_t n=-999;if(!pocket_runtime_harness_call(op,0,&n))return -999;return n;}
static int input(LinuxHost *h,int x,int y,int down,int cancel){
 PocketRuntimeContactsInput p={0};if(down){p.contact_count=1;p.contacts[0]=(PocketRuntimeContact){.id=0,.x=x,.y=y,.hit=0};}
 if(cancel){p.cancelled_count=1;p.cancelled[0]=0;}return host_turn_contacts(h,&p);
}
static int tap(LinuxHost *h,int x,int y){return input(h,x,y,1,0)&&input(h,x,y,0,0);}
static int shot(LinuxHost *h,const char *dir,const char *name,unsigned height){
 HostFrame f;char path[512];if(!host_render(h,&f))return 0;
 snprintf(path,sizeof(path),"%s/%s-%u.ppm",dir,name,height);FILE *out=fopen(path,"wb");if(!out)return 0;
 fprintf(out,"P6\n%u %u\n255\n",f.width,f.height);
 for(unsigned y=0;y<f.height;y++)for(unsigned x=0;x<f.width;x++){const unsigned char *p=f.pixels+y*f.stride+x*4;unsigned char rgb[3]={p[2],p[1],p[0]};if(fwrite(rgb,1,3,out)!=3){fclose(out);return 0;}}
 return !fclose(out);
}
#include "test_store.inc"
#include "test_render_quality.inc"
int main(int argc,char **argv){
 const char *assets=argc>1?argv[1]:"out/coffee/assets",*out=argc>2?argv[2]:"out/coffee";
 int deliberate=argc>3&&!strcmp(argv[3],"--intentional-failure");
 LinuxHost h={0};HostAsset packet={0};HostFrame f;unsigned checks=0;
 CHECK(host_asset_read(assets,"alternate.rgba",MEDIA_PACKET_BYTES,&packet));CHECK(media_packet_valid(packet.data,packet.length));
 for(unsigned height=600;height<=800;height+=200){
  const char *profile=height==600?"imx6ul-1024x600":"imx6ul-1024x800";
  CHECK(host_open(&h,profile,assets,"coffee.js","labels.atlas",0));CHECK(pocket_runtime_harness_bind("inspect"));
  CHECK(media_builtin(assets));CHECK(inspect(1)==0&&inspect(6)==1&&inspect(7)==8);
  CHECK(host_render(&h,&f));CHECK(f.height==height);
  CHECK(ui_measure_text((const uint8_t*)"咖啡",6,0)>20);CHECK(!deliberate);CHECK(shot(&h,out,"home",height));checks++;
  // Idling leaves the actual Core raster clean, not just a mocked UI state.
  CHECK(host_render(&h,&f));CHECK(pocket_runtime_damage_pixels()==0);checks++;
  for(int locale=1;locale<6;locale++){CHECK(tap(&h,890,45));CHECK(inspect(3)==locale);char name[40];snprintf(name,sizeof(name),"locale-%d",locale);CHECK(shot(&h,out,name,height));}
  CHECK(tap(&h,890,45));CHECK(inspect(3)==0);checks++;
  CHECK(tap(&h,870,(int)height-38));CHECK(inspect(4)==1);CHECK(shot(&h,out,"second-page",height));
  CHECK(tap(&h,150,170));CHECK(inspect(1)==1&&inspect(2)==6);CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==0);
  CHECK(shot(&h,out,"confirm",height));CHECK(tap(&h,330,(int)height-130));CHECK(inspect(1)==0);checks++;
  CHECK(input(&h,800,160,1,0));CHECK(input(&h,300,160,1,0));CHECK(input(&h,300,160,0,0));CHECK(inspect(4)==0&&inspect(1)==0);checks++;
  CHECK(tap(&h,150,170));CHECK(tap(&h,600,(int)height-130));CHECK(inspect(1)==2);
  CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==0);
  for(int i=0;i<145;i++){CHECK(input(&h,0,0,0,0));}CHECK(shot(&h,out,"progress",height));
  for(int i=0;i<160;i++){CHECK(input(&h,0,0,0,0));}CHECK(inspect(1)==3&&inspect(5)==1);CHECK(shot(&h,out,"done",height));
  CHECK(tap(&h,500,(int)height-90));CHECK(inspect(1)==0);checks++;
  // Cancellation must not commit the captured drink action.
  CHECK(input(&h,150,170,1,0));CHECK(input(&h,0,0,0,1));CHECK(input(&h,0,0,0,0));CHECK(inspect(1)==0);checks++;
  // Hot swap retains guest state/runtime. No shutdown or code evaluation.
  CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==1);CHECK(inspect(5)==1&&inspect(6)==2);CHECK(shot(&h,out,"updated",height));
  unsigned char old=packet.data[0];packet.data[0]='X';CHECK(!media_packet_valid(packet.data,packet.length));CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==-1);packet.data[0]=old;CHECK(inspect(6)==2);checks++;
  for(int i=0;i<24;i++){CHECK(pocket_runtime_resource_pack(packet.data,packet.length)==1);CHECK(inspect(7)==8);CHECK(host_render(&h,&f));}
  CHECK(host_alloc_stats().live_bytes<20U*1024*1024);CHECK(stage_calls>0);checks++;
  host_close(&h);CHECK(host_alloc_stats().live_bytes==0);CHECK(store_suite(assets,height));CHECK(render_quality_suite(assets,height));checks++;printf("PASS coffee-%u scenarios=10 real_core=true\n",height);
 }
cleanup:
 host_asset_free(&packet);host_close(&h);
 if(failures)return 1;
 printf("COFFEE_OK checks=%u physical_panel_validated=false\n",checks);return 0;
}
