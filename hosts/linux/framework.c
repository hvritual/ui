#define _POSIX_C_SOURCE 200809L
#include "framework.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int fail(PocketFramework *r,const char *why){if(r&&!r->error)r->error=why;return 0;}
_Static_assert(PUI_CAP_CORE==POCKET_APP_CAP_CORE && PUI_CAP_ASCII_KEYBOARD==POCKET_APP_CAP_ASCII_KEYBOARD &&
               PUI_CAP_IMAGES==POCKET_APP_CAP_IMAGES && PUI_MAX_HEAP==POCKET_APP_MAX_HEAP,"application policy mapping");
static int open_framework(PocketFramework *r,unsigned h,unsigned items,const char *assets,
                          const PuiLoadedPackage *package,const char *media_root,
                          const PocketDisplayBackend *display){
    if(!r||r->opened||r->package.impl||(!assets&&!package)||(assets&&package)||(h!=600&&h!=800))return 0;
    memset(r,0,sizeof(*r));
    const PuiPackage *manifest=package?pui_loaded_manifest(package):NULL;
    if(package){
        const uint32_t target=h==600?PUI_TARGET_600:PUI_TARGET_800;
        if(!manifest||!(manifest->targets&target)||manifest->runtime_min>PUI_RUNTIME_API||manifest->runtime_max<PUI_RUNTIME_API||
           manifest->sdk_api!=PUI_SDK_API||!(manifest->capabilities&PUI_CAP_CORE)||(manifest->capabilities&~PUI_CAP_ALL)||
           (media_root&&!(manifest->capabilities&PUI_CAP_IMAGES)))return fail(r,"PACKAGE_RUNTIME_POLICY");
        if(!pui_loaded_retain(&r->package,package))return fail(r,"PACKAGE_RETAIN");
    }
    if(display){
        if(!pocket_backend_compatible(display->abi_major,display->struct_size,sizeof(*display))||!display->present){
            pui_loaded_close(&r->package);return fail(r,"DISPLAY_CONTRACT");
        }
        r->display=*display;
    }
    if(package)pocket_scene_engine_init_package(&r->engine,&r->package);
    else pocket_scene_engine_init(&r->engine,assets);
    if(pocket_scene_engine_api.open(&r->engine,&(PocketEngineOpenConfig){1024,h,1})!=POCKET_ENGINE_OK){
        pui_loaded_close(&r->package);return fail(r,"ENGINE_OPEN");
    }
    HostAsset source={0};int loaded=0,opened=0;
    if(package){
        const PuiFile *file=pui_loaded_file(&r->package,"application.js");
        /* The existing program contract makes its own bounded source copy. */
        const PocketApplicationPolicy policy={manifest->heap_bytes,manifest->capabilities};
        opened=file&&pocket_application_open_policy(&r->app,h,items,(const char *)file->data,file->length,&policy);
    }else{
        loaded=host_asset_read(assets,"application.js",POCKET_PROGRAM_SOURCE_LIMIT,&source);
        opened=loaded&&pocket_application_open(&r->app,h,items,(const char *)source.data,source.length);
    }
    host_asset_free(&source);
    if(!opened){
        (void)pocket_scene_engine_api.close(&r->engine);pui_loaded_close(&r->package);return fail(r,"APP_OPEN");
    }
    if(!pocket_input_interaction_bridge_init(&r->input,pocket_application_interaction(&r->app))){
        pocket_application_close(&r->app);(void)pocket_scene_engine_api.close(&r->engine);
        pui_loaded_close(&r->package);return fail(r,"INPUT_BRIDGE");
    }
    r->media.root=media_root;r->opened=1;return 1;
}
int pocket_framework_open(PocketFramework *r,unsigned h,unsigned items,const char *assets,
                          const char *media_root,const PocketDisplayBackend *display){
    return open_framework(r,h,items,assets,NULL,media_root,display);
}
int pocket_framework_open_package(PocketFramework *r,unsigned h,unsigned items,const PuiLoadedPackage *package,
                                  const char *media_root,const PocketDisplayBackend *display){
    return open_framework(r,h,items,NULL,package,media_root,display);
}
int pocket_framework_input(void *p,const InputFrame *frame,uint64_t ns){
    PocketFramework *r=p;if(!r||!r->opened||r->error)return 0;
    r->event_ns=ns;
    if(ns<r->clock_ns){ns=r->clock_ns;r->timestamp_clamps++;}
    if(!pocket_application_prepare_input(&r->app))return fail(r,"INPUT_APP_EVENTS");
    if(!pocket_input_interaction_bridge_frame(&r->input,frame,ns))return fail(r,"INPUT_FRAME_REJECTED");
    r->clock_ns=ns;
    /* Process completed semantic actions after dispatch, never in callbacks. */
    if(!pocket_application_step(&r->app,ns/1000000ULL))return fail(r,"INPUT_APP_STEP");
    return 1;
}
void pocket_framework_disconnect(PocketFramework *r,uint64_t ns){
    if(!r||!r->opened)return;
    if(ns<r->clock_ns)ns=r->clock_ns;
    pocket_input_interaction_bridge_disconnect(&r->input,ns);
    r->clock_ns=ns;
}
int pocket_framework_tick(PocketFramework *r,uint64_t ns,int force){
    if(!r||!r->opened||r->error)return 0;
    if(ns<r->clock_ns)return fail(r,"CLOCK_REVERSED");
    r->clock_ns=ns;
    uint64_t begin,updated,rendered,completed;
    if(!host_monotonic_ns(&begin))return fail(r,"MEASURE_CLOCK");
    if(!pocket_application_step(&r->app,ns/1000000ULL))return fail(r,"APP_STEP");
    /* Preserve every input/application/Core tick and the accepted presentation cadence. */
    const int paint_due=force||!r->valid_frame||((r->ticks+1U)%2U==0);
    if(paint_due){
    if(!pocket_application_scene(&r->app,&r->scene))return fail(r,"SCENE_PROJECT");
    PocketEngineResource resource;
    if(pocket_scene_engine_api.resource_create(&r->engine,POCKET_SCENE_RESOURCE,&r->scene,sizeof(r->scene),&resource)!=POCKET_ENGINE_OK||
       pocket_scene_engine_api.resource_release(&r->engine,resource)!=POCKET_ENGINE_OK)return fail(r,"SCENE_UPLOAD");
    if(r->media.root&&(!r->last_media_ns||ns-r->last_media_ns>=500000000ULL)){
        (void)media_store_poll(&r->media);r->last_media_ns=ns;
        /* Bad updates retain the current image set, not a runtime failure. */
    }
    }
    uint32_t next;
    if(pocket_scene_engine_api.tick(&r->engine,ns/1000000ULL,&next)!=POCKET_ENGINE_OK)return fail(r,"ENGINE_TICK");
    r->ticks++;
    PocketApplicationStats s;if(!pocket_application_stats(&r->app,&s))return fail(r,"APP_STATS");
    if(s.page<1||s.page>32)return fail(r,"APP_ROUTE_RANGE");
    r->page_mask|=1U<<(s.page-1U);if(s.modal)r->modal_seen=1;
    if(!paint_due)return 1;
    if(!host_monotonic_ns(&updated))return fail(r,"MEASURE_CLOCK");
    if(pocket_scene_engine_api.render(&r->engine,&r->frame)!=POCKET_ENGINE_OK)return fail(r,"ENGINE_RENDER");
    if(!host_monotonic_ns(&rendered))return fail(r,"MEASURE_CLOCK");
    if(!force&&r->valid_frame&&!r->frame.damage_valid){r->clean_skips++;return 1;}
    if(r->display.present&&r->display.present(r->display.context,&r->frame)!=POCKET_ENGINE_OK)return fail(r,"DISPLAY_PRESENT");
    if(!host_monotonic_ns(&completed))return fail(r,"MEASURE_CLOCK");
    r->update_duration_ns=updated>=begin?updated-begin:0;
    r->render_duration_ns=rendered>=updated?rendered-updated:0;
    r->present_duration_ns=completed>=rendered?completed-rendered:0;
    r->present_complete_ns=completed;
    r->presented_scroll_x=s.scroll_x;r->presented_dragging=s.scroll_dragging;r->presented_settling=s.scroll_settling;
    if(s.scroll_dragging||s.scroll_settling)r->motion_presents++;
    r->frame_capturable=pocket_application_snapshot_allowed(&r->app);
    r->valid_frame=1;r->presents++;r->bytes_written+=r->frame.length;
    /* Diagnostic input-to-CPU observation only; never photon/scanout latency. */
    uint64_t end;
    if(r->event_ns&&host_monotonic_ns(&end)&&end>=r->event_ns)r->input_to_cpu_present_ns=end-r->event_ns;
    return 1;
}
int pocket_framework_can_snapshot(const PocketFramework *r){
    return r&&r->opened&&r->valid_frame&&r->frame_capturable&&pocket_application_snapshot_allowed(&r->app);
}
int pocket_framework_snapshot(const PocketFramework *r,const char *path){
    if(!path||!pocket_framework_can_snapshot(r))return 0;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);return 0;}
    const PocketEngineFrame *im=&r->frame;int ok=fprintf(f,"P6\n%u %u\n255\n",im->width,im->height)>0;
    unsigned char row[1024*3];
    for(unsigned y=0;ok&&y<im->height;y++){
        for(unsigned x=0;x<im->width;x++){
            const uint8_t *p=im->pixels+y*im->stride+x*4;
            row[x*3]=p[2];row[x*3+1]=p[1];row[x*3+2]=p[0];
        }
        if(fwrite(row,3,im->width,f)!=im->width)ok=0;
    }
    if(fclose(f))ok=0;
    return ok;
}
int pocket_framework_close(PocketFramework *r){
    if(!r)return 1;
    if(!r->opened)return pui_loaded_close(&r->package);
    pocket_framework_disconnect(r,r->clock_ns);
    pocket_application_close(&r->app);r->opened=0;
    int engine_ok=pocket_scene_engine_api.close(&r->engine)==POCKET_ENGINE_OK;
    int package_ok=pui_loaded_close(&r->package);
    return engine_ok&&package_ok;
}
