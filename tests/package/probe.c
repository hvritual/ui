#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/package/package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc,char **argv) {
    if(argc!=3)return 2;
    FILE *f=fopen(argv[2],"rb");if(!f)return 2;
    struct stat st;
    if(fstat(fileno(f),&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>PUI_MAX_BYTES){fclose(f);return 2;}
    size_t size=(size_t)st.st_size;
    unsigned char *b=malloc(size?size:1);if(!b){fclose(f);return 2;}
    size_t n=fread(b,1,size,f);int extra=fgetc(f);int bad=ferror(f);int closed=fclose(f);
    if(n!=size||extra!=EOF||bad||closed){free(b);return 2;}
    if(!strcmp(argv[1],"hash")) {
        unsigned char hash[32];pui_sha256(b,size,hash);for(unsigned i=0;i<32;i++)printf("%02x",hash[i]);puts("");free(b);return 0;
    }
    PuiPolicy policy={PUI_RUNTIME_API,PUI_SDK_API,PUI_TARGET_600,PUI_CAP_ALL,PUI_MAX_HEAP,PUI_MAX_BYTES,0};
    if(!strcmp(argv[1],"600-unsigned"))policy.allow_unsigned=1;
    else if(!strcmp(argv[1],"800-unsigned")){policy.allow_unsigned=1;policy.target=PUI_TARGET_800;}
    else if(!strcmp(argv[1],"core-only")){policy.allow_unsigned=1;policy.capabilities=PUI_CAP_CORE;}
    else if(!strcmp(argv[1],"small-heap")){policy.allow_unsigned=1;policy.max_heap_bytes=1024*1024;}
    else if(strcmp(argv[1],"strict")){free(b);return 2;}
    PuiPackage package;memset(&package,0x7f,sizeof(package));
    PuiStatus status=pui_validate(b,size,&policy,&package);
    if(status!=PUI_OK) {
        const unsigned char *p=(const unsigned char *)&package;
        for(size_t i=0;i<sizeof(package);i++)if(p[i]){free(b);return 3;}
    }
    printf("%s\n",pui_status_name(status));
    free(b);return status==PUI_OK?0:1;
}
