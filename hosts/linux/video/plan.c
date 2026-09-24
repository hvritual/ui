#define _POSIX_C_SOURCE 200809L
#include "plan.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int hash_id(const uint8_t *p) { for(unsigned i=0;i<64;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1; }
static int owned_dir(int fd) { struct stat s;return fd>=0&&!fstat(fd,&s)&&S_ISDIR(s.st_mode)&&!(s.st_mode&0022)&&s.st_uid==geteuid(); }
static int leaf(int dir,const char *name,size_t max,size_t *size) {
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat s;
 if(fd<0)return -1;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||(s.st_mode&0022)||s.st_uid!=geteuid()||s.st_size<0||(uint64_t)s.st_size>max){close(fd);return -1;}
 *size=(size_t)s.st_size;return fd;
}
static int read_all(int fd,uint8_t *data,size_t n) {
 while(n){ssize_t r=read(fd,data,n);if(r<=0)return 0;data+=r;n-=(size_t)r;}return 1;
}
void video_plan_init(VideoPlan *p) { memset(p,0,sizeof(*p));for(unsigned i=0;i<VIDEO_ASSET_LIMIT;i++)p->assets[i].fd=-1; }
void video_plan_close(VideoPlan *p) { if(!p)return;for(unsigned i=0;i<VIDEO_ASSET_LIMIT;i++)if(p->assets[i].fd>=0)close(p->assets[i].fd);video_plan_init(p); }
int video_plan_load(const char *root,const char *current,VideoPlan *out) {
 if(!root||!out)return -1;
 VideoPlan p;video_plan_init(&p);int d=-1,g=-1,f=-1,result=-1;size_t n;uint8_t data[VIDEO_INDEX_MAX],state[130];
 d=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(d<0)return 0;
 if(!owned_dir(d))goto done;
 f=leaf(d,"kind",32,&n);if(f<0)goto done;
 if(n!=14||!read_all(f,data,n)||memcmp(data,"standby-video\n",14))goto done;
 close(f);f=-1;
 f=leaf(d,"current",sizeof(state),&n);if(f<0)goto done;
 if((n!=67&&n!=130)||!read_all(f,state,n)||!hash_id(state)||state[64]!='\n')goto done;
 close(f);f=-1;memcpy(p.generation,state,64);
 if(current&&!strcmp(current,p.generation)){result=0;goto done;}
 g=openat(d,p.generation,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(!owned_dir(g))goto done;
 f=leaf(g,"index",sizeof(data),&n);if(f<0||n<VIDEO_INDEX_HEADER||!read_all(f,data,n))goto done;
 close(f);f=-1;
 if(memcmp(data,"PUIVPL1\0",8)||video_u32(data+8)!=1)goto done;
 p.asset_count=video_u32(data+12);p.item_count=video_u32(data+16);p.loop=video_u32(data+20);p.idle_ms=video_u32(data+24);p.poster=video_u32(data+28);
 if(p.asset_count<2||p.asset_count>VIDEO_ASSET_LIMIT||!p.item_count||p.item_count>VIDEO_ITEM_LIMIT||p.loop>1||p.idle_ms<1000||p.idle_ms>60000||p.poster>=p.asset_count||
    n!=VIDEO_INDEX_HEADER+p.asset_count*VIDEO_INDEX_ASSET+p.item_count*VIDEO_INDEX_ITEM)goto done;
 unsigned videos=0;
 for(unsigned i=0;i<p.asset_count;i++) {
  const uint8_t *b=data+64+i*VIDEO_INDEX_ASSET;VideoAsset *a=&p.assets[i];
  a->kind=video_u32(b);a->width=video_u32(b+4);a->height=video_u32(b+8);a->fps_num=video_u32(b+12);a->fps_den=video_u32(b+16);a->bytes=video_u32(b+20);
  if(!video_dimensions(a->width,a->height)||!hash_id(b+32))goto done;
  if(a->kind==1){if(a->fps_num||a->fps_den||a->bytes!=(size_t)a->width*a->height*4)goto done;}
  else if(a->kind==2){if(a->width%2||a->height%2||!a->fps_num||a->fps_num>30000||!a->fps_den||a->fps_den>1001||a->fps_num>30*a->fps_den||!a->bytes||a->bytes>VIDEO_FILE_BYTES)goto done;videos++;}
  else goto done;
  char name[24];snprintf(name,sizeof(name),"asset-%u",i);size_t length;
  a->fd=leaf(g,name,VIDEO_FILE_BYTES,&length);if(a->fd<0||length!=a->bytes)goto done;
 }
 if(!videos||p.assets[p.poster].kind!=1)goto done;
 for(unsigned i=0;i<p.item_count;i++) {
  const uint8_t *b=data+64+p.asset_count*VIDEO_INDEX_ASSET+i*VIDEO_INDEX_ITEM;
  VideoItem *item=&p.items[i];item->asset=video_u32(b);item->hold_ms=video_u32(b+4);
  if(item->asset>=p.asset_count)goto done;
  unsigned kind=p.assets[item->asset].kind;
  if((kind==1&&(item->hold_ms<500||item->hold_ms>60000))||(kind==2&&item->hold_ms))goto done;
 }
 *out=p;video_plan_init(&p);result=1;
 done:if(f>=0)close(f);if(g>=0)close(g);if(d>=0)close(d);video_plan_close(&p);return result;
}
