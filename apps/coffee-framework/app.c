#include "app.h"
#include "pager.h"
#include <stdlib.h>
#include <string.h>

/* Reference app: business state is simulated and cannot execute machine actions. */
enum { HOME=1,DETAIL,MAKING,SUCCESS };
enum { SELECT=1,NEXT,BACK,START,CANCEL,CONFIRM,THEME,LOCALE };
enum { STYLE_PAGE=1,STYLE_TEXT,STYLE_MUTED,STYLE_CARD,STYLE_BUTTON,STYLE_PRIMARY,STYLE_SHADE,STYLE_PROGRESS,STYLE_CLEAR };
enum { TXT_TITLE=1,TXT_SUBTITLE,TXT_DEMO,TXT_NEXT,TXT_BACK,TXT_CANCEL,TXT_START,TXT_CONFIRM,TXT_MAKING,TXT_DONE,TXT_HOME,TXT_MEDIA,TXT_LOCALE,TXT_THEME };
#define RGBA(r,g,b) (0xff000000U|((uint32_t)(b)<<16)|((uint32_t)(g)<<8)|(r))

typedef struct App App;
typedef struct {App *app;PocketUiHandle target;unsigned action,index;} Action;
typedef struct {PocketComponentHandle owner,image,text;unsigned index;} Card;
struct App {
    PocketUiTree tree;PocketLayoutContext layout;PocketStyleRuntime styles;
    PocketComponentRuntime components;PocketOverlayManager overlays;
    PocketNavigationStack nav;PocketInteractionRuntime interaction;
    PocketReactiveRuntime reactive;PocketReactiveHandle progress_signal;
    PocketVirtualCollection list;PocketComponentHandle home,grid,page_root,modal,progress;
    Action bindings[96];Card cards[COFFEE_PAGE_POOL];unsigned binding_count;
    unsigned height,item_count,first,selected,page,locale,theme,completed,actions;
    unsigned pending,pending_index;int failed,dirty;
    CoffeePager pager;int viewport_dirty;unsigned materialized_first;uint64_t layout_runs;
    uint64_t now,started,revision;
};
static int eq(PocketComponentHandle a,PocketComponentHandle b){return a.slot==b.slot&&a.generation==b.generation;}
static PocketUiHandle root_of(App *a,PocketComponentHandle c){
    PocketComponentSnapshot s;if(pocket_component_snapshot(&a->components,c,&s)!=POCKET_COMPONENT_OK)return (PocketUiHandle){0};return s.root;
}
static PocketUiEventAction event(void *context,PocketUiEvent *e){
    Action *b=context;App *a=b->app;
    if(e->phase!=POCKET_UI_EVENT_TARGET)return POCKET_UI_EVENT_CONTINUE;
    if(e->type==POCKET_UI_EVENT_TAP){
        if(a->page==HOME&&(a->pager.dragging||a->pager.settling||(b->action==SELECT&&a->pager.block_tap)))return POCKET_UI_EVENT_CONSUME;
        /* One transition per input snapshot: the first consumed action wins. */
        if(a->pending)return POCKET_UI_EVENT_CONSUME;
        a->pending=b->action;a->pending_index=b->index;a->actions++;return POCKET_UI_EVENT_CONSUME;
    }
    return POCKET_UI_EVENT_CONTINUE;
}
/* Capture-phase DOWN observes the initial coordinate before F6 slop is crossed.
 * The scroll owner stays mounted; card roots may be recycled underneath it.
 * Raw cancel to a child during gesture arbitration is NOT a cancelled scroll.
 */
static PocketUiEventAction scroll_event(void *context,PocketUiEvent *e){
    App *a=context;if(a->page!=HOME)return POCKET_UI_EVENT_CONTINUE;
    if(e->type==POCKET_UI_EVENT_POINTER_DOWN&&e->phase!=POCKET_UI_EVENT_BUBBLE){
        coffee_pager_press(&a->pager,e->pointer_id,e->x,e->timestamp_ms);
    }else if(e->type==POCKET_UI_EVENT_POINTER_UP&&e->phase!=POCKET_UI_EVENT_BUBBLE&&!a->pager.dragging){
        coffee_pager_release(&a->pager,e->pointer_id,e->x,e->timestamp_ms);
    }else if(e->phase==POCKET_UI_EVENT_TARGET){
        if(e->type==POCKET_UI_EVENT_SCROLL_BEGIN||e->type==POCKET_UI_EVENT_SCROLL_UPDATE){
            coffee_pager_move(&a->pager,e->pointer_id,e->x,e->timestamp_ms);
        }else if(e->type==POCKET_UI_EVENT_SCROLL_END){
            coffee_pager_release(&a->pager,e->pointer_id,e->x,e->timestamp_ms);
        }else if(e->type==POCKET_UI_EVENT_POINTER_CANCEL||e->type==POCKET_UI_EVENT_GESTURE_CANCEL){
            if(a->pager.pointer==e->pointer_id)coffee_pager_cancel(&a->pager);
        }
    }
    a->viewport_dirty=1;
    return POCKET_UI_EVENT_CONTINUE;
}
static int bind(App *a,PocketComponentHandle c,unsigned action,unsigned index){
    PocketUiHandle r=root_of(a,c);if(!pocket_ui_handle_valid(r))return 0;
    Action *b=NULL;
    for(unsigned i=0;i<a->binding_count;i++) {
        PocketUiSnapshot s;
        if((a->bindings[i].target.slot==r.slot&&a->bindings[i].target.generation==r.generation)||
           pocket_ui_snapshot(&a->tree,a->bindings[i].target,&s)!=POCKET_UI_OK){b=&a->bindings[i];break;}
    }
    if(!b){if(a->binding_count==96)return 0;b=&a->bindings[a->binding_count++];}
    *b=(Action){a,r,action,index};return pocket_ui_set_event_handler(&a->tree,r,event,b)==POCKET_UI_OK;
}
static int layout(App *a,PocketComponentHandle c,int x,int y,int w,int h,PocketLayoutMode mode){
    PocketLayoutSpec s=pocket_layout_spec_default();s.mode=mode;
    s.width=(PocketLength){POCKET_LENGTH_PX,w};s.height=(PocketLength){POCKET_LENGTH_PX,h};
    s.offset_x=(PocketLength){POCKET_LENGTH_PX,x};s.offset_y=(PocketLength){POCKET_LENGTH_PX,y};
    s.overflow=POCKET_OVERFLOW_CLIP;
    a->dirty=1;
    return pocket_component_set_layout(&a->components,c,&s)==POCKET_COMPONENT_OK;
}
static PocketComponentHandle make(App *a,PocketComponentKind kind,PocketComponentHandle parent,
                                 int x,int y,int w,int h,unsigned style,unsigned text,unsigned image){
    PocketComponentProps p=pocket_component_props_default(kind);p.style_ref=style;p.text_ref=text;p.resource_ref=image;
    PocketComponentHandle c={0};
    if(pocket_component_create(&a->components,kind,parent,&p,&c)!=POCKET_COMPONENT_OK||
       !layout(a,c,x,y,w,h,POCKET_LAYOUT_ABSOLUTE))a->failed=1;
    return c;
}
static PocketComponentHandle button(App *a,PocketComponentHandle parent,int x,int y,int w,
                                    unsigned text,unsigned action,unsigned index,int primary){
    PocketComponentHandle c=make(a,POCKET_COMPONENT_BUTTON,parent,x,y,w,48,primary?STYLE_PRIMARY:STYLE_BUTTON,text,0);
    if(!bind(a,c,action,index))a->failed=1;
    return c;
}
static int style_rule(App *a,unsigned ref,uint32_t bg,uint32_t fg,unsigned radius){
    PocketStyleRule r={0};r.style_ref=ref;
    r.style.set_mask=POCKET_STYLE_BIT(POCKET_STYLE_BACKGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_RADIUS);
    r.style.fields[POCKET_STYLE_BACKGROUND]=pocket_style_token(bg);
    r.style.fields[POCKET_STYLE_FOREGROUND]=pocket_style_token(fg);
    r.style.fields[POCKET_STYLE_RADIUS]=pocket_style_literal(radius);
    return pocket_style_add_rule(&a->styles,&r)==POCKET_STYLE_OK;
}
static int themes(App *a){
    for(unsigned i=0;i<2;i++){
        PocketThemeToken tokens[]={ {1,i?RGBA(23,30,29):RGBA(248,246,240)},
         {2,i?RGBA(235,242,236):RGBA(48,55,52)}, {3,i?RGBA(38,49,45):RGBA(255,255,255)},
         {4,i?RGBA(60,80,67):RGBA(231,236,225)}, {5,RGBA(52,99,75)},
         {6,RGBA(255,255,255)}, {7,0x90000000U}, {8,0},
         {9,i?RGBA(181,196,185):RGBA(113,118,111)} };
        PocketThemeDefinition t={.id=i+1,.tokens=tokens,.token_count=9};
        t.base.set_mask=POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_FONT_SIZE);
        t.base.fields[POCKET_STYLE_FOREGROUND]=pocket_style_token(2);
        t.base.fields[POCKET_STYLE_FONT_SIZE]=pocket_style_literal(22);
        if(pocket_style_add_theme(&a->styles,&t)!=POCKET_STYLE_OK)return 0;
    }
    return style_rule(a,STYLE_PAGE,1,2,0)&&style_rule(a,STYLE_TEXT,8,2,0)&&
      style_rule(a,STYLE_MUTED,8,9,0)&&style_rule(a,STYLE_CARD,3,2,12)&&
      style_rule(a,STYLE_BUTTON,4,2,10)&&style_rule(a,STYLE_PRIMARY,5,6,10)&&
      style_rule(a,STYLE_SHADE,7,2,0)&&style_rule(a,STYLE_PROGRESS,4,5,7)&&
      style_rule(a,STYLE_CLEAR,8,2,0)&&pocket_style_set_theme(&a->styles,1)==POCKET_STYLE_OK;
}
static uint32_t count(void *p){return ((App *)p)->item_count;}
static int key(void *p,uint32_t i,uint64_t *out){if(i>=((App *)p)->item_count||!out)return 0;*out=i+1;return 1;}
static PocketComponentStatus delegate(void *p,uint32_t i,uint64_t k,PocketComponentRuntime *rt,PocketComponentHandle c){
    App *a=p;(void)k;unsigned pos=i%COFFEE_PAGE_ITEMS;int rh=((int)a->height-186)/2;
    if(!layout(a,c,(int)(i/COFFEE_PAGE_ITEMS)*COFFEE_PAGE_WIDTH-a->pager.position+(int)(pos%3)*328,(int)(pos/3)*(rh+16),304,rh,POCKET_LAYOUT_ABSOLUTE)||
       pocket_component_set_style_ref(rt,c,STYLE_CARD)!=POCKET_COMPONENT_OK)return POCKET_COMPONENT_UI_ERROR;
    Card *card=NULL;
    for(unsigned j=0;j<COFFEE_PAGE_POOL;j++)if(eq(a->cards[j].owner,c)){card=&a->cards[j];break;}
    if(!card)for(unsigned j=0;j<COFFEE_PAGE_POOL;j++)if(!pocket_component_handle_valid(a->cards[j].owner)){card=&a->cards[j];card->owner=c;break;}
    if(!card)return POCKET_COMPONENT_RESOURCE_EXHAUSTED;
    card->index=i;
    if(!pocket_component_handle_valid(card->image)){
        card->image=make(a,POCKET_COMPONENT_IMAGE,c,24,8,256,128,STYLE_CLEAR,0,i%8+1);
        card->text=make(a,POCKET_COMPONENT_TEXT,c,20,rh-42,270,36,STYLE_TEXT,100+i%8,0);
    }
    if(a->failed||pocket_component_set_resource_ref(rt,card->image,i%8+1)!=POCKET_COMPONENT_OK||
       pocket_component_set_text_ref(rt,card->text,100+i%8)!=POCKET_COMPONENT_OK||!bind(a,c,SELECT,i))return POCKET_COMPONENT_UI_ERROR;
    return POCKET_COMPONENT_OK;
}
static PocketComponentHandle page(App *a,unsigned heading){
    PocketComponentHandle c=make(a,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0,0,1024,a->height,STYLE_PAGE,0,0);
    make(a,POCKET_COMPONENT_TEXT,c,32,20,620,36,STYLE_TEXT,heading,0);
    make(a,POCKET_COMPONENT_TEXT,c,32,a->height-52,730,36,STYLE_MUTED,TXT_DEMO,0);
    return c;
}
static int sync_layout(App *a){
    PocketNavigationPage p;
    if(pocket_navigation_top(&a->nav,&p)!=POCKET_NAV_OK)return 0;
    PocketUiHandle r=root_of(a,p.root);
    if(!a->dirty)return 1;
    if(pocket_layout_run(&a->layout,r,1024,a->height)!=POCKET_UI_OK)return 0;
    a->layout_runs++;a->dirty=0;
    if(pocket_component_handle_valid(a->modal)&&pocket_layout_run(&a->layout,root_of(a,a->modal),1024,a->height)!=POCKET_UI_OK)return 0;
    return pocket_interaction_set_scene_root(&a->interaction,r)==POCKET_INTERACTION_OK;
}
static int dismiss(App *a){
    if(!pocket_component_handle_valid(a->modal))return 1;
    if(pocket_overlay_dismiss(&a->overlays,1)!=POCKET_OVERLAY_OK)return 0;
    a->modal=(PocketComponentHandle){0};return 1;
}
static int modal(App *a){
    if(pocket_component_handle_valid(a->modal))return 0;
    a->modal=make(a,POCKET_COMPONENT_MODAL,(PocketComponentHandle){0},0,0,1024,a->height,STYLE_SHADE,0,0);
    PocketComponentHandle panel=make(a,POCKET_COMPONENT_VIEW,a->modal,240,160,544,260,STYLE_CARD,0,0);
    make(a,POCKET_COMPONENT_TEXT,panel,32,24,480,36,STYLE_TEXT,TXT_CONFIRM,0);
    make(a,POCKET_COMPONENT_TEXT,panel,32,80,480,36,STYLE_TEXT,100+a->selected%8,0);
    button(a,panel,32,180,220,TXT_CANCEL,CANCEL,0,0);
    button(a,panel,280,180,232,TXT_START,START,0,1);
    return !a->failed&&pocket_overlay_present(&a->overlays,&(PocketOverlaySpec){.id=1,.owner_route=DETAIL,
      .kind=POCKET_OVERLAY_MODAL,.root=a->modal,.focus_token=((uint64_t)a->modal.generation<<32)|a->modal.slot,
      .owns_root=1,.captures_input=1,.captures_focus=1})==POCKET_OVERLAY_OK;
}
static int detail(App *a){
    a->page_root=page(a,TXT_CONFIRM);
    make(a,POCKET_COMPONENT_IMAGE,a->page_root,384,156,256,128,STYLE_CLEAR,0,a->selected%8+1);
    make(a,POCKET_COMPONENT_TEXT,a->page_root,384,318,512,36,STYLE_TEXT,100+a->selected%8,0);
    button(a,a->page_root,256,a->height-136,224,TXT_BACK,BACK,0,0);
    button(a,a->page_root,520,a->height-136,240,TXT_START,CONFIRM,0,1);
    a->page=DETAIL;
    return !a->failed&&pocket_navigation_push(&a->nav,&(PocketNavigationPage){DETAIL,a->page_root,1})==POCKET_NAV_OK;
}
static int making(App *a){
    if(!dismiss(a))return 0;
    PocketComponentHandle c=page(a,TXT_MAKING);
    make(a,POCKET_COMPONENT_IMAGE,c,384,156,256,128,STYLE_CLEAR,0,a->selected%8+1);
    make(a,POCKET_COMPONENT_TEXT,c,384,318,512,36,STYLE_TEXT,100+a->selected%8,0);
    a->progress=make(a,POCKET_COMPONENT_PROGRESS,c,256,384,512,16,STYLE_PROGRESS,0,0);
    PocketReactiveSubscription subscription;
    if(pocket_reactive_bind_component(&a->reactive,a->progress_signal,a->progress,POCKET_BIND_VALUE,&subscription)!=POCKET_REACTIVE_OK)return 0;
    button(a,c,384,a->height-115,256,TXT_CANCEL,CANCEL,0,0);
    if(pocket_reactive_set(&a->reactive,a->progress_signal,pocket_value_i64(0))!=POCKET_REACTIVE_OK)return 0;
    a->started=a->now;a->page=MAKING;a->page_root=c;
    return !a->failed&&pocket_navigation_replace(&a->nav,&(PocketNavigationPage){MAKING,c,1})==POCKET_NAV_OK;
}
static int success(App *a){
    PocketComponentHandle c=page(a,TXT_DONE);
    make(a,POCKET_COMPONENT_IMAGE,c,384,156,256,128,STYLE_CLEAR,0,a->selected%8+1);
    make(a,POCKET_COMPONENT_TEXT,c,384,318,512,36,STYLE_TEXT,100+a->selected%8,0);
    button(a,c,384,a->height-115,256,TXT_HOME,BACK,0,1);
    a->page=SUCCESS;a->page_root=c;a->completed++;
    return !a->failed&&pocket_navigation_replace(&a->nav,&(PocketNavigationPage){SUCCESS,c,1})==POCKET_NAV_OK;
}
static int viewport(App *a){
    int pos=a->pager.position;unsigned last=(a->item_count-1)/COFFEE_PAGE_ITEMS;
    unsigned page=pos>0?(unsigned)pos/COFFEE_PAGE_WIDTH:0;
    if(page>=last)page=last?last-1:0;
    unsigned first=page*COFFEE_PAGE_ITEMS;
    if(first!=a->materialized_first){
        unsigned count=a->item_count-first;if(count>COFFEE_PAGE_POOL)count=COFFEE_PAGE_POOL;
        if(pocket_virtual_collection_set_window(&a->list,first,count)!=POCKET_MODEL_OK)return 0;
        a->materialized_first=first;
    }
    int rh=((int)a->height-186)/2;
    for(unsigned i=0;i<COFFEE_PAGE_POOL;i++){
        Card *c=&a->cards[i];if(!pocket_component_handle_valid(c->owner))continue;
        unsigned n=c->index,cell=n%COFFEE_PAGE_ITEMS;
        if(!layout(a,c->owner,(int)(n/COFFEE_PAGE_ITEMS)*COFFEE_PAGE_WIDTH-pos+(int)(cell%3)*328,
                   (int)(cell/3)*(rh+16),304,rh,POCKET_LAYOUT_ABSOLUTE))return 0;
    }
    a->viewport_dirty=0;return 1;
}
static int window(App *a,unsigned first){
    /* Button paging cancels all held contacts before recycling. Dragging itself
     * captures the permanent scroll container, not a recycled card. */
    if(a->interaction.impl)pocket_interaction_cancel_all(&a->interaction,a->now);
    coffee_pager_jump(&a->pager,first/COFFEE_PAGE_ITEMS);a->first=first;
    return viewport(a);
}
static int act(App *a,unsigned what,unsigned index){
    a->dirty=1;
    switch(what){
    case SELECT:if(a->page!=HOME||index>=a->item_count)return 0;a->selected=index;return detail(a);
    case NEXT:if(a->page!=HOME)return 1;return window(a,a->first+6<a->item_count?a->first+6:0);
    case BACK:if(a->page==HOME)return window(a,a->first>=6?a->first-6:0);
        if(!dismiss(a)||pocket_navigation_pop(&a->nav)!=POCKET_NAV_OK)return 0;
        a->page=HOME;return 1;
    case CONFIRM:return a->page==DETAIL&&modal(a);
    case START:return a->page==DETAIL&&making(a);
    case CANCEL:if(pocket_component_handle_valid(a->modal))return dismiss(a);
        if(a->page==MAKING){if(pocket_navigation_pop(&a->nav)!=POCKET_NAV_OK)return 0;a->page=HOME;}return 1;
    case THEME:a->theme=1-a->theme;return pocket_style_set_theme(&a->styles,a->theme+1)==POCKET_STYLE_OK;
    case LOCALE:a->locale=(a->locale+1)%6;return 1;
    default:return 0;
    }
}
int coffee_app_init(CoffeeApp *out,unsigned h,unsigned items){
    if(!out||out->impl||(h!=600&&h!=800)||(items!=8&&items!=100))return 0;
    App *a=calloc(1,sizeof(*a));if(!a)return 0;out->impl=a;a->height=h;a->item_count=items;a->page=HOME;
    coffee_pager_init(&a->pager,items);a->materialized_first=UINT32_MAX;
    PocketUiTreeConfig tc={.initial_capacity=128,.update_queue_capacity=64,.update_budget=64};
    PocketLayoutConfig lc={.tree=&a->tree,.record_capacity=256};
    PocketStyleRuntimeConfig sc={.theme_capacity=2,.token_capacity=32,.rule_capacity=32};
    PocketComponentRuntimeConfig cc={.tree=&a->tree,.layout=&a->layout,.styles=&a->styles,.capacity=256};
    PocketOverlayConfig oc={.components=&a->components,.capacity=8};
    PocketNavigationConfig nc={.components=&a->components,.capacity=4,.owner_cleanup=pocket_overlay_navigation_cleanup,.owner_cleanup_context=&a->overlays};
    PocketReactiveConfig rc={.node_capacity=16,.effect_capacity=8,.binding_capacity=16,.default_flush_budget=64,.components=&a->components};
    if(pocket_ui_tree_init(&a->tree,&tc)!=POCKET_UI_OK||pocket_layout_init(&a->layout,&lc)!=POCKET_UI_OK||
       pocket_style_runtime_init(&a->styles,&sc)!=POCKET_STYLE_OK||!themes(a)||
       pocket_component_runtime_init(&a->components,&cc)!=POCKET_COMPONENT_OK||pocket_overlay_init(&a->overlays,&oc)!=POCKET_OVERLAY_OK||
       pocket_navigation_init(&a->nav,&nc)!=POCKET_NAV_OK||pocket_reactive_init(&a->reactive,&rc)!=POCKET_REACTIVE_OK||
       pocket_reactive_signal(&a->reactive,pocket_value_i64(0),&a->progress_signal)!=POCKET_REACTIVE_OK)goto fail;
    a->home=page(a,TXT_TITLE);
    make(a,POCKET_COMPONENT_TEXT,a->home,32,55,600,36,STYLE_MUTED,TXT_SUBTITLE,0);
    button(a,a->home,672,24,144,TXT_THEME,THEME,0,0);button(a,a->home,832,24,160,TXT_LOCALE,LOCALE,0,0);
    a->grid=make(a,POCKET_COMPONENT_SCROLL,a->home,32,105,960,h-170,STYLE_CLEAR,0,0);
    button(a,a->home,784,h-60,208,TXT_NEXT,NEXT,0,0);
    PocketVirtualCollectionConfig vc={.components=&a->components,.parent=a->grid,.item_kind=POCKET_COMPONENT_BUTTON,
       .overscan=0,.max_pool=COFFEE_PAGE_POOL,.model={a,count,key,delegate}};
    if(a->failed||pocket_virtual_collection_init(&a->list,&vc)!=POCKET_MODEL_OK||!window(a,0)||
       pocket_navigation_push(&a->nav,&(PocketNavigationPage){HOME,a->home,1})!=POCKET_NAV_OK)goto fail;
    PocketInteractionConfig ic={.tree=&a->tree,.layout=&a->layout,.components=&a->components,.overlays=&a->overlays,.scene_root=root_of(a,a->home)};
    if(pocket_interaction_init(&a->interaction,&ic)!=POCKET_INTERACTION_OK||
       pocket_ui_set_event_handler(&a->tree,root_of(a,a->grid),scroll_event,a)!=POCKET_UI_OK||
       pocket_interaction_set_gestures(&a->interaction,root_of(a,a->grid),POCKET_GESTURE_SCROLL_X)!=POCKET_INTERACTION_OK||!sync_layout(a))goto fail;
    return 1;
fail:coffee_app_dispose(out);return 0;
}
int coffee_app_step(CoffeeApp *out,uint64_t ms){
    App *a=out?out->impl:NULL;if(!a||a->failed||ms<a->now)return 0;a->now=ms;
    if(pocket_interaction_tick(&a->interaction,ms)!=POCKET_INTERACTION_OK)return 0;
    if(a->page==HOME){
        PocketInteractionSnapshot input;
        if(pocket_interaction_snapshot(&a->interaction,&input)!=POCKET_INTERACTION_OK)return 0;
        if(!input.active_pointers&&a->pager.active){coffee_pager_cancel(&a->pager);a->viewport_dirty=1;}
        int previous=a->pager.position;coffee_pager_tick(&a->pager,ms);
        if(previous!=a->pager.position)a->viewport_dirty=1;
        if(a->viewport_dirty&&!viewport(a))return 0;
        a->first=a->pager.settled_page*COFFEE_PAGE_ITEMS;
    }
    if(a->pending){unsigned action=a->pending,index=a->pending_index;a->pending=0;if(!act(a,action,index))return 0;}
    if(a->page==MAKING){unsigned progress=(unsigned)((ms-a->started)/50);if(progress>100)progress=100;
      if(pocket_reactive_set(&a->reactive,a->progress_signal,pocket_value_i64(progress))!=POCKET_REACTIVE_OK)return 0;
      if(progress==100&&!success(a))return 0;
    }
    PocketReactiveFlushStats flush;
    if(pocket_reactive_flush(&a->reactive,64,&flush)!=POCKET_REACTIVE_OK||!sync_layout(a))return 0;
    return pocket_interaction_tick(&a->interaction,ms)==POCKET_INTERACTION_OK&&!a->failed;
}
PocketInteractionRuntime *coffee_app_interaction(CoffeeApp *a){return a&&a->impl?&((App *)a->impl)->interaction:NULL;}
int coffee_app_scene(CoffeeApp *out,PocketScene *s){
    App *a=out?out->impl:NULL;PocketNavigationPage p;PocketInteractionSnapshot input;
    if(!a||!s||pocket_navigation_top(&a->nav,&p)!=POCKET_NAV_OK||pocket_interaction_snapshot(&a->interaction,&input)!=POCKET_INTERACTION_OK)return 0;
    PocketSceneSource src={&a->tree,&a->components,&a->layout,&a->styles};
    if(!pocket_scene_begin(s,1024,a->height,a->locale,a->theme?RGBA(23,30,29):RGBA(248,246,240),
       a->page==HOME&&!input.active_pointers&&!a->pager.settling&&!a->pager.dragging&&!pocket_overlay_count(&a->overlays))||!pocket_scene_append(s,&src,root_of(a,p.root)))return 0;
    return !pocket_component_handle_valid(a->modal)||pocket_scene_append(s,&src,root_of(a,a->modal));
}
int coffee_app_stats(const CoffeeApp *out,CoffeeAppStats *s){
    const App *a=out?out->impl:NULL;if(!a||!s)return 0;PocketVirtualCollectionStats v;PocketReactiveValue value;
    if(pocket_virtual_collection_stats(&a->list,&v)!=POCKET_MODEL_OK||pocket_reactive_get(&a->reactive,a->progress_signal,&value)!=POCKET_REACTIVE_OK)return 0;
    *s=(CoffeeAppStats){a->page,a->selected,a->first,a->item_count,a->locale,a->theme,(unsigned)value.as.i64,
      a->completed,(unsigned)pocket_overlay_count(&a->overlays),(unsigned)pocket_ui_live_count(&a->tree),v.pool_size,v.peak_pool_size,a->actions,v.recycle_count,
      a->pager.position,(unsigned)a->pager.dragging,(unsigned)a->pager.settling,a->layout_runs};return 1;
}
void coffee_app_dispose(CoffeeApp *out){
    App *a=out?out->impl:NULL;if(!a)return;
    pocket_interaction_dispose(&a->interaction);pocket_reactive_dispose(&a->reactive);
    pocket_virtual_collection_dispose(&a->list);pocket_overlay_dispose(&a->overlays);pocket_navigation_dispose(&a->nav);
    pocket_component_runtime_dispose(&a->components);pocket_style_runtime_dispose(&a->styles);pocket_layout_dispose(&a->layout);pocket_ui_tree_dispose(&a->tree);
    free(a);out->impl=NULL;
}
