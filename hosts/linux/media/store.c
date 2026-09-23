#define _POSIX_C_SOURCE 200809L
#include "store.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static uint32_t word(const unsigned char *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t crc(const unsigned char *data,size_t size) {
 uint32_t n=~0U;
 for(size_t i=0;i<size;++i){n^=data[i];for(unsigned j=0;j<8;++j)n=(n>>1)^(0xedb88320U&((uint32_t)0-(n&1)));}
 return ~n;
}
int media_packet_valid(const unsigned char *p,size_t n) {
 return p && n==MEDIA_PACKET_BYTES && !memcmp(p,"PUIIMG1\0",8) && word(p+8)==1 &&
 word(p+12)==8 && word(p+16)==256 && word(p+20)==128 && word(p+24)==n-64 && word(p+28)==crc(p+64,n-64);
}
int media_scene_valid(const unsigned char *p,size_t n) {
 if(!p||n<32||n>2U*1024U*1024U||memcmp(p,"PUISCNE1",8)||word(p+8)!=1||word(p+24)||word(p+28))return 0;
 uint32_t meta=word(p+12),pixels=word(p+16);
 return meta>0&&meta<=32768&&pixels<=1792U*1024U&&n==32U+meta+pixels&&word(p+20)==crc(p+32,n-32);
}
int media_scene_clock(uint64_t ns) {
 unsigned char packet[16]="PUITICK1";uint64_t ms=ns/1000000ULL;
 for(unsigned i=0;i<8;++i)packet[8+i]=(unsigned char)(ms>>(i*8));
 return pocket_runtime_resource_pack(packet,sizeof(packet))==1;
}
int media_scene_builtin(const char *assets) {
 HostAsset a={0};int ok=0;
 if(host_asset_read(assets,"scene.packet",2U*1024U*1024U,&a)&&media_scene_valid(a.data,a.length))
  ok=pocket_runtime_resource_pack(a.data,a.length)==1;
 host_asset_free(&a);return ok;
}
static int digest(const unsigned char *p) {
 for(unsigned i=0;i<64;++i) if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
 return 1;
}
int media_builtin(const char *assets) {
 HostAsset a={0};int ok=0;
 if(host_asset_read(assets,"builtin.rgba",MEDIA_PACKET_BYTES,&a)&&media_packet_valid(a.data,a.length))
  ok=pocket_runtime_resource_pack(a.data,a.length)==1;
 host_asset_free(&a);return ok;
}
int media_store_poll(MediaStore *s) {
 if(!s || !s->root)return 0;
 struct stat st;int directory=open(s->root,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(directory<0)return 0;
 int trusted=!fstat(directory,&st) && S_ISDIR(st.st_mode) && !(st.st_mode&0022) && st.st_uid==geteuid();
 close(directory);if(!trusted)return -1;
 HostAsset state={0},packet={0};char id[65]={0},name[80];int result=0;
 if(!host_asset_read(s->root,"current",130,&state))goto done;
 if((state.length!=67 && state.length!=130)||!digest(state.data)||state.data[64]!='\n')goto done;
 memcpy(id,state.data,64);
 if(!strcmp(id,s->applied)||!strcmp(id,s->rejected))goto done;
 if(pocket_runtime_resource_pack(NULL,0)!=1){++s->deferred_count;goto done;}
 snprintf(name,sizeof(name),"%s.rgba",id);
 if(!host_asset_read(s->root,name,s->scene?2U*1024U*1024U:MEDIA_PACKET_BYTES,&packet)||!(s->scene?media_scene_valid(packet.data,packet.length):media_packet_valid(packet.data,packet.length))){
  memcpy(s->rejected,id,65);++s->rejected_count;fprintf(stderr,"MEDIA_REJECTED generation=%s reason=packet-integrity old-retained=true\n",id);result=-1;goto done;
 }
 result=pocket_runtime_resource_pack(packet.data,packet.length);
 if(result==1){memcpy(s->applied,id,65);++s->applied_count;fprintf(stderr,"MEDIA_APPLIED generation=%s\n",id);}
 else if(result==0){++s->deferred_count;}
 else{memcpy(s->rejected,id,65);++s->rejected_count;fprintf(stderr,"MEDIA_REJECTED generation=%s reason=guest old-retained=true\n",id);}
 done:host_asset_free(&packet);host_asset_free(&state);return result;
}
