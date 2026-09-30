#include "scene_runtime.h"
#include "../media/store.h"
#include <stdio.h>
#include <string.h>

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
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
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
    uint8_t buf[POCKET_SCENE_WIRE_MAX];
    const size_t n=POCKET_SCENE_WIRE_HEADER+(size_t)s->count*POCKET_SCENE_WIRE_RECORD;
    const uint32_t header[]={POCKET_SCENE_WIRE_MAGIC,s->width,s->height,s->count,
                             s->locale,s->background,(uint32_t)!!s->media_ready,0};
    for(unsigned i=0;i<8;i++)put32(buf+i*4,header[i]);
    for(uint32_t i=0;i<s->count;i++) {
        const PocketSceneRecord *r=&s->records[i];
        if(!r->id||r->id>9007199254740991ULL||r->bounds.width<0||r->bounds.height<0||
           r->clip.width<0||r->clip.height<0||r->opacity_256>256||
           r->text_ref>65535||r->resource_ref>8)return POCKET_ENGINE_INVALID_ARGUMENT;
        for(uint32_t j=0;j<i;j++)if(s->records[j].id==r->id)return POCKET_ENGINE_INVALID_ARGUMENT;
        const uint32_t words[]={ (uint32_t)r->id,(uint32_t)(r->id>>32),r->kind,
            (uint32_t)r->bounds.x,(uint32_t)r->bounds.y,(uint32_t)r->bounds.width,(uint32_t)r->bounds.height,
            (uint32_t)r->clip.x,(uint32_t)r->clip.y,(uint32_t)r->clip.width,(uint32_t)r->clip.height,
            r->background,r->foreground,r->radius,r->opacity_256,(uint32_t)r->text_ref,
            (uint32_t)r->resource_ref,(uint32_t)r->value,(uint32_t)r->minimum,(uint32_t)r->maximum};
        uint8_t *record=buf+POCKET_SCENE_WIRE_HEADER+(size_t)i*POCKET_SCENE_WIRE_RECORD;
        for(unsigned j=0;j<20;j++)put32(record+j*4,words[j]);
    }
    if(n==e->previous_length&&!memcmp(buf,e->previous,n))e->scene_skips++;
    else {
        if(e->generation==UINT32_MAX)return POCKET_ENGINE_RESOURCE_EXHAUSTED;
        if(pocket_runtime_resource_pack((const uint8_t *)buf,n)!=1)return POCKET_ENGINE_BACKEND_FAILED;
        memcpy(e->previous,buf,n);e->previous_length=n;e->generation++;e->scene_uploads++;e->scene_wire_bytes+=n;
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
