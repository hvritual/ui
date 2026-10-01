#include "scene_runtime.h"
#include "../media/store.h"
#include "../text-input/input_font.h"
#include "private_scene_guest.generated.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The adapter bytes belong to this ELF, not to a developer-editable .js asset.
 * Encode every catalog byte before parsing it as JSON in the renderer context.
 * Quotes, backslashes, invalid UTF-8 and JS-looking data cannot execute code. */
static int boot_scene(LinuxHost *host,const char *profile,const char *root,const PuiLoadedPackage *package) {
    static const char prefix[]="const POCKET_TEXT_CATALOG=(()=>{try{const c=JSON.parse(decodeURIComponent('";
    static const char suffix[]="'));if(!Array.isArray(c)||c.length!==6)throw 0;"
        "for(const t of c){if(!t||typeof t!=='object'||Array.isArray(t)||Object.keys(t).length>4096)throw 0;"
        "for(const k of Object.keys(t)){if(!/^[0-9]+$/.test(k)||Number(k)>65535||typeof t[k]!=='string'||t[k].length>4096)throw 0;}}"
        "return c;}catch(_){throw Error('CATALOG_INVALID');}})();\n";
    static const char hex[]="0123456789ABCDEF";
    HostAsset catalog={0};
    const PuiFile *entry=package?pui_loaded_file(package,"catalog.json"):NULL;
    const unsigned char *catalog_bytes;
    size_t catalog_length;
    if(package){
        if(!entry||!entry->length||entry->length>65536)return 0;
        catalog_bytes=entry->data;catalog_length=entry->length;
    }else{
        if(!host_asset_read(root,"catalog.json",65536,&catalog))return 0;
        catalog_bytes=catalog.data;catalog_length=catalog.length;
    }
    size_t length=sizeof(prefix)-1+catalog_length*3+sizeof(suffix)-1+sizeof(pocket_scene_guest)-1;
    char *source=malloc(length+1);
    if(!source){host_asset_free(&catalog);return 0;}
    char *p=source;memcpy(p,prefix,sizeof(prefix)-1);p+=sizeof(prefix)-1;
    for(size_t i=0;i<catalog_length;i++){unsigned char c=catalog_bytes[i];*p++='%';*p++=hex[c>>4];*p++=hex[c&15];}
    memcpy(p,suffix,sizeof(suffix)-1);p+=sizeof(suffix)-1;
    memcpy(p,pocket_scene_guest,sizeof(pocket_scene_guest)-1);source[length]=0;
    host_asset_free(&catalog);
    int ok;
    if(package){
        const PuiFile *atlas=pui_loaded_file(package,"labels.atlas");
        ok=atlas&&host_open_buffers(host,profile,source,length,atlas->data,atlas->length,0);
    }else ok=host_open_source(host,profile,root,source,length,"labels.atlas",0);
    free(source);return ok;
}
void pocket_scene_engine_init(PocketSceneEngine *e,const char *root) {
    if(e){memset(e,0,sizeof(*e));e->asset_root=root;}
}
void pocket_scene_engine_init_package(PocketSceneEngine *e,const PuiLoadedPackage *package) {
    if(e){memset(e,0,sizeof(*e));e->package=package;}
}
static int input_font_upload(const PuiFile *font) {
    if(!font||!pocket_input_font_valid(font->data,font->length))return 0;
    const size_t chunk=1024u*1024u;
    unsigned char *wire=malloc(chunk+16);if(!wire)return 0;
    int ok=1;
    for(size_t offset=0;offset<font->length;){
        size_t length=font->length-offset;if(length>chunk)length=chunk;
        const uint32_t header[]={0x31464950u,(uint32_t)font->length,(uint32_t)offset,(uint32_t)length};
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)wire[i*4+j]=(unsigned char)(header[i]>>(j*8));
        memcpy(wire+16,font->data+offset,length);
        if(pocket_runtime_resource_pack(wire,length+16)!=1){ok=0;break;}
        offset+=length;
    }
    free(wire);return ok;
}
static PocketEngineStatus open_engine(void *p,const PocketEngineOpenConfig *c) {
    PocketSceneEngine *e=p;
    if(!e||e->opened||!c||c->width!=1024||(c->height!=600&&c->height!=800)||
       c->target_api_level!=1)return POCKET_ENGINE_INVALID_ARGUMENT;
    const char *profile=c->height==600?"imx6ul-1024x600":"imx6ul-1024x800";
    if(!boot_scene(&e->host,profile,e->asset_root,e->package))return POCKET_ENGINE_BACKEND_FAILED;
    if(e->package){
        const PuiPackage *m=pui_loaded_manifest(e->package);
        if(m&&(m->capabilities&PUI_CAP_PINYIN)){
            const PuiFile *font=pui_loaded_file(e->package,"input.atlas");
            if(!input_font_upload(font)){
                host_close(&e->host);return POCKET_ENGINE_BACKEND_FAILED;
            }
        }
    }
    int media_ok=0;
    if(e->package){
        const PuiPackage *manifest=pui_loaded_manifest(e->package);
        const PuiFile *image=pui_loaded_file(e->package,"builtin.rgba");
        if(manifest&&!(manifest->capabilities&PUI_CAP_IMAGES))media_ok=1;
        else if(image&&media_packet_valid(image->data,image->length))
            media_ok=pocket_runtime_resource_pack(image->data,image->length)==1;
    }else media_ok=media_builtin(e->asset_root);
    if(!media_ok){host_close(&e->host);return POCKET_ENGINE_BACKEND_FAILED;}
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
           r->text_ref>131071||r->resource_ref>8)return POCKET_ENGINE_INVALID_ARGUMENT;
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
