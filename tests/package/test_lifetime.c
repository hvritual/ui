#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/framework.h"
#include "tests/framework/frame_oracle.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"PACKAGE_LIFETIME_FAIL line=%d %s\n",__LINE__,#x);return 0;} } while(0)
static int copy_file(const char *source,const char *dest) {
    FILE *in=fopen(source,"rb");CHECK(in);
    FILE *out=fopen(dest,"wb");CHECK(out);unsigned char bytes[4096];size_t n;
    while((n=fread(bytes,1,sizeof(bytes),in))!=0)CHECK(fwrite(bytes,1,n,out)==n);
    CHECK(!ferror(in));CHECK(!fclose(in));CHECK(!fclose(out));return 1;
}
static int tap(PocketFramework *runtime,uint64_t *clock,int x,int y) {
    InputFrame down={.contact_count=1,.contacts={{0,x,y}}},up={0};
    *clock+=20000000ULL;CHECK(pocket_framework_input(runtime,&down,*clock));
    *clock+=20000000ULL;CHECK(pocket_framework_input(runtime,&up,*clock));
    CHECK(pocket_framework_tick(runtime,*clock,1));return 1;
}
static int immutable(const char *path,const char *scratch,unsigned height) {
    char file[4096];CHECK(snprintf(file,sizeof(file),"%s/retained-%u.pui",scratch,height)<(int)sizeof(file));
    CHECK(copy_file(path,file));
    PuiLoadedPackage loaded={0},copy={0};PuiPolicy policy=pui_runtime_policy(height,1);
    PuiLoadedSnapshot first,second;const char *error=NULL;
    CHECK(pui_loaded_open(&loaded,file,&policy,&error));CHECK(error==NULL);
    CHECK(!pui_loaded_open(&loaded,file,&policy,&error));
    CHECK(pui_loaded_snapshot(&loaded,&first));CHECK(pui_loaded_retain(&copy,&loaded));
    CHECK(pui_loaded_close(&loaded));CHECK(!loaded.impl);
    const PuiFile *source=pui_loaded_file(&copy,"application.js");CHECK(source&&source->length);
    unsigned char digest[32],after[32];pui_sha256(source->data,source->length,digest);
    /* Change and remove the original inode after admission. */
    FILE *f=fopen(file,"wb");CHECK(f);CHECK(fputs("REPLACED_AFTER_VERIFICATION",f)>=0);CHECK(!fclose(f));
    CHECK(!pui_loaded_open(&loaded,file,&policy,&error));CHECK(!loaded.impl);
    CHECK(unlink(file)==0);
    pui_sha256(source->data,source->length,after);CHECK(!memcmp(digest,after,sizeof(digest)));
    CHECK(pui_loaded_snapshot(&copy,&second));CHECK(!memcmp(&first,&second,sizeof(first)));
    /* A deliberately faulting test-only child proves OS write protection.
     * Reset sanitizer's signal handler; this probe never enters a delivery ELF. */
    pid_t child=fork();CHECK(child>=0);
    if(!child){
        const struct rlimit no_core={0,0};(void)setrlimit(RLIMIT_CORE,&no_core);
        signal(SIGSEGV,SIG_DFL);signal(SIGBUS,SIG_DFL);
        volatile unsigned char *attempt=(volatile unsigned char *)(uintptr_t)source->data;
        *attempt^=1;_exit(90);
    }
    int status=0;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status)&&(WTERMSIG(status)==SIGSEGV||WTERMSIG(status)==SIGBUS));
    PocketFramework *runtime=calloc(1,sizeof(*runtime));CHECK(runtime);
    FrameOracle oracle;CHECK(oracle_open(&oracle,1024,height));
    PocketDisplayBackend display={1,sizeof(display),&oracle,oracle_present};
    CHECK(pocket_framework_open_package(runtime,height,8,&copy,NULL,&display));
    CHECK(pui_loaded_close(&copy));CHECK(!copy.impl);
    uint64_t now=1000000000ULL;CHECK(pocket_framework_tick(runtime,now,1));
    CHECK(tap(runtime,&now,180,(int)height-124));
    PocketApplicationStats s;CHECK(pocket_application_stats(&runtime->app,&s));CHECK(s.selected==1);
    char image[4096];CHECK(snprintf(image,sizeof(image),"%s/retained-%u.ppm",scratch,height)<(int)sizeof(image));
    CHECK(pocket_framework_snapshot(runtime,image));
    CHECK(pocket_framework_close(runtime));CHECK(pocket_framework_close(runtime));
    CHECK(!runtime->package.impl&&!host_alloc_stats().live_bytes);free(runtime);oracle_close(&oracle);
    return 1;
}
static int rejection_and_cleanup(const char *path,const char *scratch) {
    PuiLoadedPackage loaded={0};const char *error=NULL;PuiPolicy policy=pui_runtime_policy(600,0);
    CHECK(!pui_loaded_open(&loaded,path,&policy,&error));CHECK(!loaded.impl&&!strcmp(error,"PUI_UNSIGNED"));
    policy=pui_runtime_policy(600,1);
    char name[4096];CHECK(snprintf(name,sizeof(name),"%s/pipe",scratch)<(int)sizeof(name));
    CHECK(mkfifo(name,0600)==0);CHECK(!pui_loaded_open(&loaded,name,&policy,&error));
    CHECK(!loaded.impl&&!strcmp(error,"PUI_LOAD_REGULAR"));CHECK(unlink(name)==0);
    CHECK(symlink(path,name)==0);CHECK(!pui_loaded_open(&loaded,name,&policy,&error));
    CHECK(!loaded.impl&&!strcmp(error,"PUI_LOAD_OPEN"));CHECK(unlink(name)==0);
    CHECK(!pui_loaded_open(&loaded,scratch,&policy,&error));CHECK(!loaded.impl);
    for(unsigned i=0;i<20;++i){
        CHECK(pui_loaded_open(&loaded,path,&policy,&error));
        PocketFramework *runtime=calloc(1,sizeof(*runtime));CHECK(runtime);
        PocketDisplayBackend invalid={0};
        CHECK(!pocket_framework_open_package(runtime,600,8,&loaded,NULL,&invalid));
        CHECK(!runtime->package.impl&&!runtime->opened);
        CHECK(pui_loaded_close(&loaded));CHECK(pocket_framework_close(runtime));free(runtime);
    }
    CHECK(!host_alloc_stats().live_bytes);return 1;
}
int main(int argc,char **argv) {
    if(argc!=3&&argc!=4)return 2;
    if(argc==4){
        PuiLoadedPackage package={0};PuiPolicy policy=pui_runtime_policy(600,1);const char *error=NULL;
        if(!pui_loaded_open(&package,argv[1],&policy,&error))return 2;
        PuiLoadedSnapshot s;if(!pui_loaded_snapshot(&package,&s))return 2;
        int wrong=s.container_bytes==0;
        if(!pui_loaded_close(&package))return 2;
        if(!wrong)fprintf(stderr,"INTENTIONAL_PACKAGE_LIFETIME_ASSERTION_FAILURE\n");
        return wrong?0:1;
    }
    if(!rejection_and_cleanup(argv[1],argv[2])||!immutable(argv[1],argv[2],600)||!immutable(argv[1],argv[2],800))return 1;
    puts("PACKAGE_LIFETIME_OK immutable retained dual-viewport cleanup no-physical");return 0;
}
