/* Integration coverage for the actual store reader and real running guest. */
#include <unistd.h>
static int store_write(const char *dir,const char *name,const void *p,size_t n){
 char path[512];snprintf(path,sizeof(path),"%s/%s",dir,name);FILE *f=fopen(path,"wb");if(!f)return 0;
 int ok=fwrite(p,1,n,f)==n;int closed=fclose(f);return ok&&!closed;
}
static int store_current(const char *dir,char id){
 char value[68],path[512],next[512];memset(value,id,64);memcpy(value+64,"\n-\n",3);
 if(!store_write(dir,".next",value,67))return 0;
 snprintf(path,sizeof(path),"%s/current",dir);snprintf(next,sizeof(next),"%s/.next",dir);return rename(next,path)==0;
}
static int store_suite(const char *assets,unsigned height){
 LinuxHost host={0};HostAsset a={0},b={0};MediaStore store={0};char dir[]="/tmp/coffee-store-XXXXXX",filename[80],path[512];int made=0,ok=0;
 const char *profile=height==600?"imx6ul-1024x600":"imx6ul-1024x800";
 #define STORE_CHECK(x) do{if(!(x)){fprintf(stderr,"COFFEE_STORE_FAIL line=%d %s\n",__LINE__,#x);goto done;}}while(0)
 STORE_CHECK(mkdtemp(dir));made=1;store.root=dir;
 STORE_CHECK(host_asset_read(assets,"builtin.rgba",MEDIA_PACKET_BYTES,&a));
 STORE_CHECK(host_asset_read(assets,"alternate.rgba",MEDIA_PACKET_BYTES,&b));
 STORE_CHECK(host_open(&host,profile,assets,"coffee.js","labels.atlas",0));STORE_CHECK(pocket_runtime_harness_bind("inspect"));STORE_CHECK(media_builtin(assets));
 STORE_CHECK(media_store_poll(&store)==0&&inspect(6)==1);
 memset(filename,'a',64);memcpy(filename+64,".rgba",6);STORE_CHECK(store_write(dir,filename,a.data,a.length));
 memset(filename,'b',64);memcpy(filename+64,".rgba",6);STORE_CHECK(store_write(dir,filename,b.data,b.length));
 STORE_CHECK(store_current(dir,'b'));STORE_CHECK(media_store_poll(&store)==1&&inspect(6)==2&&store.applied_count==1);
 STORE_CHECK(media_store_poll(&store)==0&&inspect(6)==2);
 STORE_CHECK(tap(&host,150,170)&&inspect(1)==1);STORE_CHECK(store_current(dir,'a'));
 STORE_CHECK(media_store_poll(&store)==0&&inspect(6)==2&&store.deferred_count==1);
 STORE_CHECK(tap(&host,330,(int)height-130)&&inspect(1)==0);STORE_CHECK(media_store_poll(&store)==1&&inspect(6)==3);
 b.data[b.length-1]^=1;memset(filename,'c',64);memcpy(filename+64,".rgba",6);STORE_CHECK(store_write(dir,filename,b.data,b.length));b.data[b.length-1]^=1;
 STORE_CHECK(store_current(dir,'c'));STORE_CHECK(media_store_poll(&store)==-1&&store.rejected_count==1&&inspect(6)==3);
 STORE_CHECK(media_store_poll(&store)==0&&store.rejected_count==1);
 STORE_CHECK(store_current(dir,'b'));STORE_CHECK(media_store_poll(&store)==1&&inspect(6)==4);
 host_close(&host);STORE_CHECK(host_alloc_stats().live_bytes==0);
 memset(&store,0,sizeof(store));store.root=dir;
 STORE_CHECK(host_open(&host,profile,assets,"coffee.js","labels.atlas",0));STORE_CHECK(pocket_runtime_harness_bind("inspect"));STORE_CHECK(media_builtin(assets));
 STORE_CHECK(media_store_poll(&store)==1&&inspect(6)==2);
 STORE_CHECK(store_write(dir,"current","../untrusted\n",13));STORE_CHECK(media_store_poll(&store)==0&&inspect(6)==2);
 ok=1;
 done:host_close(&host);host_asset_free(&a);host_asset_free(&b);
 if(made){for(char id='a';id<='c';id++){memset(filename,id,64);memcpy(filename+64,".rgba",6);snprintf(path,sizeof(path),"%s/%s",dir,filename);unlink(path);}snprintf(path,sizeof(path),"%s/current",dir);unlink(path);snprintf(path,sizeof(path),"%s/.next",dir);unlink(path);rmdir(dir);}
 #undef STORE_CHECK
 return ok;
}
