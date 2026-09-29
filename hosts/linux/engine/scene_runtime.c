#include "scene_runtime.h"
#include "../media/store.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>

void pocket_scene_engine_init(PocketSceneEngine *e,const char *root) {
    if(e){memset(e,0,sizeof(*e));e->asset_root=root;}
}
static PocketEngineStatus open_engine(void *p,const PocketEngineOpenConfig *c) {
    PocketSceneEngine *e=p;
    if(!e||e->opened||!c||c->width!=1024||(c->height!=600&&c->height!=800)||
       c->target_api_level!=1)return POCKET_ENGINE_INVALID_ARGUMENT;
    const char *profile=c->height==600?"imx6ul-1024x600":"imx6ul-1024x800";
    if(!host_open(&e->host,profile,e->asset_root,"framework.js","labels.atlas",0))
        return POCKET_ENGINE_BACKEND_FAILED;
    if(!media_builtin(e->asset_root)){host_close(&e->host);return POCKET_ENGINE_BACKEND_FAILED;}
    e->opened=1;e->generation=1;return POCKET_ENGINE_OK;
}
static PocketEngineStatus close_engine(void *p) {
    PocketSceneEngine *e=p;if(!e||!e->opened)return POCKET_ENGINE_LIFECYCLE_ERROR;
    host_close(&e->host);e->opened=0;e->previous_length=0;return POCKET_ENGINE_OK;
}
static PocketEngineStatus caps(void *p,PocketEngineCapabilities *c) {
    PocketSceneEngine *e=p;if(!e||!e->opened||!c)return POCKET_ENGINE_LIFECYCLE_ERROR;
    *c=pocket_scene_engine_api.capabilities;return POCKET_ENGINE_OK;
}
static PocketEngineStatus tick(void *p,uint64_t ms,uint32_t *deadline) {
    PocketSceneEngine *e=p;(void)ms;
    if(!e||!e->opened||!deadline)return POCKET_ENGINE_LIFECYCLE_ERROR;
    if(!host_turn(&e->host,NULL))return POCKET_ENGINE_BACKEND_FAILED;
    *deadline=17;return POCKET_ENGINE_OK;
}
static PocketEngineStatus render(void *p,PocketEngineFrame *out) {
    PocketSceneEngine *e=p;HostFrame f;
    if(!e||!e->opened||!out)return POCKET_ENGINE_LIFECYCLE_ERROR;
    if(!host_render(&e->host,&f))return POCKET_ENGINE_BACKEND_FAILED;
    memset(out,0,sizeof(*out));out->width=f.width;out->height=f.height;
    out->stride=f.stride;out->pixels=f.pixels;out->length=f.length;
    int b[4];out->damage_valid=pocket_runtime_damage_bounds(b);
    if(out->damage_valid)out->damage=(PocketEngineRect){b[0],b[1],b[2]-b[0],b[3]-b[1]};
    return POCKET_ENGINE_OK;
}
static int append(char *buf,size_t *used,const char *fmt,...) {
    if(*used>=POCKET_SCENE_JSON_MAX)return 0;
    va_list args;va_start(args,fmt);int n=vsnprintf(buf+*used,POCKET_SCENE_JSON_MAX-*used,fmt,args);va_end(args);
    if(n<0||(size_t)n>=POCKET_SCENE_JSON_MAX-*used)return 0;
    *used+=(size_t)n;return 1;
}
static PocketEngineStatus upload(void *p,uint32_t kind,const void *data,size_t length,
                                PocketEngineResource *out) {
    PocketSceneEngine *e=p;
    if(!e||!e->opened)return POCKET_ENGINE_LIFECYCLE_ERROR;
    if(kind!=POCKET_SCENE_RESOURCE)return POCKET_ENGINE_UNSUPPORTED;
    if(!data||length!=sizeof(PocketScene)||!out)return POCKET_ENGINE_INVALID_ARGUMENT;
    const PocketScene *s=data;
    if(s->version!=1||s->count>POCKET_SCENE_MAX_RECORDS||s->width!=e->host.width||
       s->height!=e->host.height||s->locale>=6)return POCKET_ENGINE_INVALID_ARGUMENT;
    char buf[POCKET_SCENE_JSON_MAX];size_t n=0;
    if(!append(buf,&n,"{\"v\":1,\"w\":%u,\"h\":%u,\"locale\":%u,\"bg\":%u,\"ready\":%d,\"nodes\":[",
               s->width,s->height,s->locale,s->background,!!s->media_ready))return POCKET_ENGINE_RESOURCE_EXHAUSTED;
    for(uint32_t i=0;i<s->count;i++) {
        const PocketSceneRecord *r=&s->records[i];
        if(!r->id||r->id>9007199254740991ULL||r->bounds.width<0||r->bounds.height<0||
           r->clip.width<0||r->clip.height<0||r->opacity_256>256||
           r->text_ref>65535||r->resource_ref>8)return POCKET_ENGINE_INVALID_ARGUMENT;
        for(uint32_t j=0;j<i;j++)if(s->records[j].id==r->id)return POCKET_ENGINE_INVALID_ARGUMENT;
        if(!append(buf,&n,"%s[%" PRIu64 ",%u,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%d,%d,%d]",
          i?",":"",r->id,r->kind,r->bounds.x,r->bounds.y,r->bounds.width,r->bounds.height,
          r->clip.x,r->clip.y,r->clip.width,r->clip.height,r->background,r->foreground,
          r->radius,r->opacity_256,r->text_ref,r->resource_ref,r->value,r->minimum,r->maximum))
            return POCKET_ENGINE_RESOURCE_EXHAUSTED;
    }
    if(!append(buf,&n,"]}"))return POCKET_ENGINE_RESOURCE_EXHAUSTED;
    if(n==e->previous_length&&!memcmp(buf,e->previous,n))e->scene_skips++;
    else {
        if(e->generation==UINT32_MAX)return POCKET_ENGINE_RESOURCE_EXHAUSTED;
        if(pocket_runtime_resource_pack((const uint8_t *)buf,n)!=1)return POCKET_ENGINE_BACKEND_FAILED;
        memcpy(e->previous,buf,n);e->previous_length=n;e->generation++;e->scene_uploads++;
    }
    e->ready=s->media_ready;*out=(PocketEngineResource){1,e->generation};return POCKET_ENGINE_OK;
}
static PocketEngineStatus release(void *p,PocketEngineResource r) {
    PocketSceneEngine *e=p;if(!e||!e->opened)return POCKET_ENGINE_LIFECYCLE_ERROR;
    return r.slot==1&&r.generation==e->generation?POCKET_ENGINE_OK:POCKET_ENGINE_STALE_HANDLE;
}
const PocketEngineApi pocket_scene_engine_api={
    .abi_major=1,.abi_minor=0,.struct_size=sizeof(PocketEngineApi),
    .name="current-pocket-scene",.capabilities=POCKET_ENGINE_CAP_FRAME_RENDER|
       POCKET_ENGINE_CAP_DAMAGE|POCKET_ENGINE_CAP_TIMER_DEADLINE|POCKET_ENGINE_CAP_RESOURCE,
    .open=open_engine,.close=close_engine,.query_capabilities=caps,
    .tick=tick,.render=render,.resource_create=upload,.resource_release=release
};
