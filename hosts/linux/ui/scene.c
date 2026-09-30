#include "scene.h"
#include <string.h>
#include <limits.h>

int pocket_scene_begin(PocketScene *s,uint32_t w,uint32_t h,uint32_t locale,
                       uint32_t background,int ready) {
    if(!s||w!=1024||(h!=600&&h!=800)||locale>=6)return 0;
    memset(s,0,sizeof(*s));s->version=POCKET_SCENE_VERSION;
    s->width=w;s->height=h;s->locale=locale;s->background=background;
    s->media_ready=!!ready;return 1;
}
static int supported(PocketComponentKind k) {
    return k==POCKET_COMPONENT_VIEW||k==POCKET_COMPONENT_TEXT||
           k==POCKET_COMPONENT_IMAGE||k==POCKET_COMPONENT_BUTTON||
           k==POCKET_COMPONENT_PROGRESS||k==POCKET_COMPONENT_GRID||
           k==POCKET_COMPONENT_SCROLL||k==POCKET_COMPONENT_LIST||
           k==POCKET_COMPONENT_MODAL||k==POCKET_COMPONENT_DIALOG||
           k==POCKET_COMPONENT_LOADING||k==POCKET_COMPONENT_TEXT_FIELD;
}
static int visit(PocketScene *s,const PocketSceneSource *src,PocketUiHandle root,
                 const PocketResolvedStyle *parent,uint16_t alpha,unsigned depth) {
    if(depth>=128||s->count>=POCKET_SCENE_MAX_RECORDS)return 0;
    PocketUiSnapshot n;PocketComponentSnapshot c;PocketLayoutResult l;
    if(pocket_ui_snapshot(src->tree,root,&n)!=POCKET_UI_OK)return 0;
    if(!n.properties.visible||n.phase==POCKET_UI_PHASE_UNMOUNTED||
       n.phase==POCKET_UI_PHASE_CREATED)return 1;
    if(pocket_component_from_root(src->components,root,&c)!=POCKET_COMPONENT_OK||
       !supported(c.kind)||pocket_layout_result(src->layout,root,&l)!=POCKET_UI_OK)return 0;
    PocketResolvedStyle style;
    if(pocket_style_resolve(src->styles,n.properties.style_ref,
                           (uint32_t)n.properties.semantic_state,parent,&style)!=POCKET_STYLE_OK)return 0;
    /* Geometry transforms must also enter hit testing before being admitted. */
    if(style.fields[POCKET_STYLE_TRANSLATE_X]||style.fields[POCKET_STYLE_TRANSLATE_Y]||
       style.fields[POCKET_STYLE_BORDER_WIDTH]||style.fields[POCKET_STYLE_FONT_ID]||
       style.fields[POCKET_STYLE_SPACING]||style.fields[POCKET_STYLE_FONT_SIZE]!=22)return 0;
    alpha=(uint16_t)((uint32_t)alpha*n.properties.opacity_256/256U);
    if(style.set_mask&POCKET_STYLE_BIT(POCKET_STYLE_OPACITY))
        alpha=(uint16_t)((uint32_t)alpha*(uint32_t)style.fields[POCKET_STYLE_OPACITY]/256U);
    /* Current adapter supports RGBA text/background alpha, but not image-node opacity. */
    if(c.kind==POCKET_COMPONENT_IMAGE&&alpha!=256)return 0;
    if(n.stable_id>9007199254740991ULL||c.props.text_ref>65535||c.props.resource_ref>8)return 0;
    PocketSceneRecord *r=&s->records[s->count++];
    r->id=n.stable_id;r->kind=(uint32_t)c.kind;
    r->bounds=(PocketEngineRect){l.geometry.x,l.geometry.y,l.geometry.width,l.geometry.height};
    r->clip=l.clip_valid?(PocketEngineRect){l.clip.x,l.clip.y,l.clip.width,l.clip.height}:
                         (PocketEngineRect){0,0,(int32_t)s->width,(int32_t)s->height};
    r->background=(uint32_t)style.fields[POCKET_STYLE_BACKGROUND];
    r->foreground=(uint32_t)style.fields[POCKET_STYLE_FOREGROUND];
    r->radius=(uint16_t)style.fields[POCKET_STYLE_RADIUS];r->opacity_256=alpha;
    r->text_ref=c.props.text_ref;r->resource_ref=c.props.resource_ref;
    r->value=c.props.value;r->minimum=c.props.min_value;r->maximum=c.props.max_value;
    PocketUiHandle children[POCKET_LAYOUT_MAX_CHILDREN],child={0};unsigned count=0;
    if(pocket_ui_first_child(src->tree,root,&child)!=POCKET_UI_OK)return 0;
    while(pocket_ui_handle_valid(child)) {
        if(count==POCKET_LAYOUT_MAX_CHILDREN)return 0;
        children[count++]=child;
        if(pocket_ui_next_sibling(src->tree,child,&child)!=POCKET_UI_OK)return 0;
    }
    /* Object siblings are newest-first; paint oldest-first, matching F6 top hit. */
    while(count)if(!visit(s,src,children[--count],&style,alpha,depth+1U))return 0;
    return 1;
}
int pocket_scene_append(PocketScene *s,const PocketSceneSource *src,PocketUiHandle root) {
    if(!s||!src||!src->tree||!src->components||!src->layout||!src->styles||
       s->version!=POCKET_SCENE_VERSION||s->count>POCKET_SCENE_MAX_RECORDS)return 0;
    uint32_t old=s->count;
    if(!visit(s,src,root,NULL,256,0)){s->count=old;return 0;}
    return 1;
}
