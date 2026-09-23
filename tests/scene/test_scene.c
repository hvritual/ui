#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "media/store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned stages, presents;
void pocket_bench_stage(int stage){(void)stage;++stages;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"SCENE_FAIL line=%d %s error=%s\n",__LINE__,#x,pocket_runtime_error());goto fail;}}while(0)
static int inspect(int op){int32_t n=-1;if(!pocket_runtime_harness_call(op,0,&n))return -999;return n;}
static int frame(LinuxHost *h,uint64_t ms,int x,int y,int down,int cancel){
 PocketRuntimeContactsInput in={0};if(down){in.contact_count=1;in.contacts[0]=(PocketRuntimeContact){.id=0,.x=x,.y=y,.hit=0};}
 if(cancel){in.cancelled_count=1;in.cancelled[0]=0;}
 return media_scene_clock(ms*1000000ULL)&&host_turn_contacts(h,&in);
}
static int tap(LinuxHost *h,uint64_t ms,int x,int y){return frame(h,ms,x,y,1,0)&&frame(h,ms+1,x,y,0,0);}
static int present(void *ctx,const HostFrame *f){(void)ctx;if(!f||!f->pixels)return 0;++presents;return 1;}
static int shot(LinuxHost *h,const char *dir,const char *name,unsigned height){
 HostFrame f;char path[512];if(!host_render(h,&f))return 0;
 snprintf(path,sizeof(path),"%s/%s-%u.ppm",dir,name,height);FILE *out=fopen(path,"wb");if(!out)return 0;
 fprintf(out,"P6\n%u %u\n255\n",f.width,f.height);
 for(unsigned y=0;y<f.height;y++)for(unsigned x=0;x<f.width;x++){const uint8_t *p=f.pixels+y*f.stride+x*4;uint8_t rgb[3]={p[2],p[1],p[0]};if(fwrite(rgb,1,3,out)!=3){fclose(out);return 0;}}
 return !fclose(out);
}
static int write_file(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(p,1,n,f)==n;return fclose(f)==0&&ok;}
static int suite(const char *assets,const char *out,unsigned height,int deliberate){
 LinuxHost h={0};HostAsset b={0},hidden={0},empty={0};HostFrame f;unsigned count=0;
 char dir[128]="/tmp/scene-store-XXXXXX",path[240],state[68];int have_dir=0;
 MediaStore store={.scene=1};
 CHECK(host_open(&h,height==600?"imx6ul-1024x600":"imx6ul-1024x800",assets,"media-scene.js","labels.atlas",0));
 CHECK(pocket_runtime_harness_bind("inspect"));CHECK(media_scene_builtin(assets));
 CHECK(frame(&h,0,0,0,0,0));CHECK(inspect(1)==2&&inspect(3)==9&&inspect(4)==1);CHECK(!deliberate);count++;
 CHECK(shot(&h,out,"scene-a",height));
 presents=0;CHECK(host_render(&h,&f));CHECK(host_present_latest(&h,&f,1,present,NULL));
 for(unsigned ms=10;ms<=1000;ms+=10){CHECK(frame(&h,ms,0,0,0,0));CHECK(host_render(&h,&f));CHECK(pocket_runtime_damage_pixels()==0);CHECK(host_present_latest(&h,&f,0,present,NULL));}
 CHECK(presents==1&&h.clean_frames_skipped==100);count++;
 CHECK(frame(&h,2000,0,0,0,0));CHECK(inspect(7)==1);CHECK(shot(&h,out,"carousel-second",height));
 CHECK(frame(&h,5000,0,0,0,0));CHECK(inspect(7)==2);CHECK(frame(&h,6500,0,0,0,0));CHECK(inspect(7)==0);count++;
 CHECK(tap(&h,6600,100,(int)height-45));CHECK(inspect(6)==1);
 int index=inspect(7);CHECK(frame(&h,20000,0,0,0,0));CHECK(inspect(7)==index);count++;
 CHECK(tap(&h,20010,500,(int)height-45));CHECK(inspect(7)==1);
 CHECK(tap(&h,20020,320,(int)height-45));CHECK(inspect(7)==0);count++;
 CHECK(tap(&h,20030,100,(int)height-45));CHECK(inspect(6)==0);
 CHECK(host_asset_read(assets,"scene-b.packet",2U*1024*1024,&b));CHECK(media_scene_valid(b.data,b.length));
 CHECK(frame(&h,20100,500,200,1,0));CHECK(pocket_runtime_resource_pack(b.data,b.length)==0);CHECK(inspect(1)==2);
 CHECK(frame(&h,20110,500,200,0,1));CHECK(frame(&h,20120,0,0,0,0));
 CHECK(pocket_runtime_resource_pack(b.data,b.length)==1);CHECK(inspect(1)==3&&inspect(2)==3&&inspect(3)==9);CHECK(shot(&h,out,"scene-b",height));count++;
 unsigned char old=b.data[0];b.data[0]='X';CHECK(!media_scene_valid(b.data,b.length));CHECK(pocket_runtime_resource_pack(b.data,b.length)==-1);b.data[0]=old;CHECK(inspect(1)==3);count++;
 CHECK(host_asset_read(assets,"scene-hidden.packet",2U*1024*1024,&hidden));
 CHECK(pocket_runtime_resource_pack(hidden.data,hidden.length)==1);CHECK(inspect(1)==3&&inspect(2)==2);CHECK(shot(&h,out,"scene-hidden",height));count++;
 CHECK(host_asset_read(assets,"scene-empty.packet",2U*1024*1024,&empty));
 CHECK(pocket_runtime_resource_pack(empty.data,empty.length)==1);CHECK(inspect(1)==0&&inspect(2)==0);CHECK(shot(&h,out,"scene-empty",height));count++;
 for(int i=0;i<40;i++){CHECK(pocket_runtime_resource_pack(b.data,b.length)==1);CHECK(host_render(&h,&f));CHECK(pocket_runtime_resource_pack(empty.data,empty.length)==1);CHECK(host_render(&h,&f));}
 CHECK(host_alloc_stats().live_bytes<16U*1024*1024);count++;
 // Real local current-file delivery, rejection and rollback, not only JS calls.
 CHECK(mkdtemp(dir));have_dir=1;store.root=dir;snprintf(path,sizeof(path),"%s/%064d.rgba",dir,1);CHECK(write_file(path,b.data,b.length));
 snprintf(state,sizeof(state),"%064d\n-\n",1);snprintf(path,sizeof(path),"%s/current",dir);CHECK(write_file(path,state,67));
 CHECK(media_store_poll(&store)==1&&inspect(1)==3);CHECK(media_store_poll(&store)==0);
 snprintf(path,sizeof(path),"%s/%064d.rgba",dir,2);CHECK(write_file(path,"bad",3));snprintf(state,sizeof(state),"%064d\n-\n",2);
 snprintf(path,sizeof(path),"%s/current",dir);CHECK(write_file(path,state,67));CHECK(media_store_poll(&store)==-1&&inspect(1)==3);CHECK(media_store_poll(&store)==0);
 snprintf(path,sizeof(path),"%s/%064d.rgba",dir,3);CHECK(write_file(path,empty.data,empty.length));snprintf(state,sizeof(state),"%064d\n-\n",3);
 snprintf(path,sizeof(path),"%s/current",dir);CHECK(write_file(path,state,67));CHECK(media_store_poll(&store)==1&&inspect(1)==0);
 snprintf(state,sizeof(state),"%064d\n-\n",1);CHECK(write_file(path,state,67));CHECK(media_store_poll(&store)==1&&inspect(1)==3);count++;
 CHECK(stages>0);host_close(&h);CHECK(host_alloc_stats().live_bytes==0);count++;
 if(have_dir){for(int i=1;i<=3;i++){snprintf(path,sizeof(path),"%s/%064d.rgba",dir,i);unlink(path);}snprintf(path,sizeof(path),"%s/current",dir);unlink(path);rmdir(dir);}
 host_asset_free(&b);host_asset_free(&hidden);host_asset_free(&empty);
 printf("PASS media-scene-%u checks=%u real_core=true\n",height,count);return count==12;
 fail:host_close(&h);host_asset_free(&b);host_asset_free(&hidden);host_asset_free(&empty);return 0;
}
int main(int argc,char **argv){if(argc<3)return 2;int fail=argc==4;if(!suite(argv[1],argv[2],600,fail)||!suite(argv[1],argv[2],800,fail))return 1;puts("SCENE_OK checks=24 physical_hardware=false");return 0;}
