#ifndef POCKET_PACKAGE_TEST_ADAPTER_H
#define POCKET_PACKAGE_TEST_ADAPTER_H
#include "hosts/linux/framework.h"
#include <stdio.h>
static int package_test_open(PocketFramework *runtime,unsigned height,unsigned items,
                             const char *assets,const char *media,const PocketDisplayBackend *display) {
    char path[4096];
    if(!assets||snprintf(path,sizeof(path),"%s.pui",assets)>=(int)sizeof(path))return 0;
    PuiLoadedPackage package={0};PuiPolicy policy=pui_runtime_policy(height,1);
    const char *error=NULL;
    if(!pui_loaded_open(&package,path,&policy,&error))return 0;
    int ok=pocket_framework_open_package(runtime,height,items,&package,media,display);
    if(!pui_loaded_close(&package))return 0;
    return ok;
}
#define pocket_framework_open package_test_open
#endif
