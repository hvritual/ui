#include "hosts/linux/ui/interaction.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"INTERACTION_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct { int down,move,up,cancel,key,focus_gained,focus_lost; } Trace;
static PocketUiEventAction handler(void *context,PocketUiEvent *event) {
    Trace *trace=context;
    if(event->type==POCKET_UI_EVENT_POINTER_DOWN)trace->down++;
    else if(event->type==POCKET_UI_EVENT_POINTER_MOVE)trace->move++;
    else if(event->type==POCKET_UI_EVENT_POINTER_UP)trace->up++;
    else if(event->type==POCKET_UI_EVENT_POINTER_CANCEL)trace->cancel++;
    else if(event->type==POCKET_UI_EVENT_KEY_ACTION)trace->key++;
    else if(event->type==POCKET_UI_EVENT_FOCUS_GAINED)trace->focus_gained++;
    else if(event->type==POCKET_UI_EVENT_FOCUS_LOST)trace->focus_lost++;
    return POCKET_UI_EVENT_CONTINUE;
}
static PocketComponentHandle add(PocketComponentRuntime *components,PocketComponentKind kind,
                                 PocketComponentHandle parent) {
    PocketComponentHandle h={0};
    if(pocket_component_create(components,kind,parent,NULL,&h)!=POCKET_COMPONENT_OK)
        return (PocketComponentHandle){0};
    return h;
}
static int absolute(PocketComponentRuntime *components,PocketComponentHandle component,
                     int x,int y,int w,int h) {
    PocketLayoutSpec s=pocket_layout_spec_default();
    s.width=(PocketLength){POCKET_LENGTH_PX,w};
    s.height=(PocketLength){POCKET_LENGTH_PX,h};
    s.offset_x=(PocketLength){POCKET_LENGTH_PX,x};
    s.offset_y=(PocketLength){POCKET_LENGTH_PX,y};
    CHECK(pocket_component_set_layout(components,component,&s)==POCKET_COMPONENT_OK);
    return 0;
}
int main(void) {
    PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=16,.update_queue_capacity=16,.update_budget=8};
    CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
    PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=64};
    CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
    PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=32};
    CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);
    PocketOverlayManager overlays={0};PocketOverlayConfig oc={.components=&components,.capacity=8};
    CHECK(pocket_overlay_init(&overlays,&oc)==POCKET_OVERLAY_OK);

    PocketComponentHandle scene=add(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0});
    PocketComponentHandle button=add(&components,POCKET_COMPONENT_BUTTON,scene);
    PocketComponentHandle field=add(&components,POCKET_COMPONENT_TEXT_FIELD,scene);
    CHECK(pocket_component_handle_valid(scene)&&pocket_component_handle_valid(field));
    PocketLayoutSpec root=pocket_layout_spec_default();root.mode=POCKET_LAYOUT_ABSOLUTE;
    CHECK(pocket_component_set_layout(&components,scene,&root)==POCKET_COMPONENT_OK);
    CHECK(absolute(&components,button,10,10,100,60)==0);
    CHECK(absolute(&components,field,10,100,180,50)==0);
    PocketComponentSnapshot ss,bs,fs;
    CHECK(pocket_component_snapshot(&components,scene,&ss)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_snapshot(&components,button,&bs)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_snapshot(&components,field,&fs)==POCKET_COMPONENT_OK);
    CHECK(pocket_layout_run(&layout,ss.root,320,240)==POCKET_UI_OK);

    Trace background={0},focus_trace={0};
    CHECK(pocket_ui_set_event_handler(&tree,bs.root,handler,&background)==POCKET_UI_OK);
    CHECK(pocket_ui_set_event_handler(&tree,fs.root,handler,&focus_trace)==POCKET_UI_OK);

    PocketInteractionRuntime interaction={0};
    PocketInteractionConfig ic={.tree=&tree,.layout=&layout,.components=&components,
                                .overlays=&overlays,.scene_root=ss.root};
    CHECK(pocket_interaction_init(&interaction,&ic)==POCKET_INTERACTION_OK);

    PocketPointerEvent down={1,POCKET_POINTER_DOWN,20,20,10};
    CHECK(pocket_interaction_pointer(&interaction,&down)==POCKET_INTERACTION_OK&&background.down==1);
    CHECK(pocket_interaction_capture(&interaction,1,bs.root)==POCKET_INTERACTION_OK);
    PocketPointerEvent move={1,POCKET_POINTER_MOVE,300,220,20};
    CHECK(pocket_interaction_pointer(&interaction,&move)==POCKET_INTERACTION_OK&&background.move==1);
    PocketPointerEvent up={1,POCKET_POINTER_UP,300,220,30};
    CHECK(pocket_interaction_pointer(&interaction,&up)==POCKET_INTERACTION_OK&&background.up==1);

    PocketPointerEvent focus_down={2,POCKET_POINTER_DOWN,20,110,40};
    CHECK(pocket_interaction_pointer(&interaction,&focus_down)==POCKET_INTERACTION_OK);
    PocketInteractionSnapshot state;
    CHECK(pocket_interaction_snapshot(&interaction,&state)==POCKET_INTERACTION_OK);
    CHECK(state.focused.slot==fs.root.slot&&focus_trace.focus_gained==1);
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_OK&&focus_trace.key==1);
    PocketPointerEvent focus_up={2,POCKET_POINTER_UP,20,110,50};
    CHECK(pocket_interaction_pointer(&interaction,&focus_up)==POCKET_INTERACTION_OK);

    PocketComponentHandle modal=add(&components,POCKET_COMPONENT_MODAL,(PocketComponentHandle){0});
    PocketComponentHandle modal_button=add(&components,POCKET_COMPONENT_BUTTON,modal);
    PocketLayoutSpec modal_root=pocket_layout_spec_default();modal_root.mode=POCKET_LAYOUT_ABSOLUTE;
    CHECK(pocket_component_set_layout(&components,modal,&modal_root)==POCKET_COMPONENT_OK);
    CHECK(absolute(&components,modal_button,10,10,100,60)==0);
    PocketComponentSnapshot ms,mbs;
    CHECK(pocket_component_snapshot(&components,modal,&ms)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_snapshot(&components,modal_button,&mbs)==POCKET_COMPONENT_OK);
    CHECK(pocket_layout_run(&layout,ms.root,320,240)==POCKET_UI_OK);
    Trace modal_trace={0};
    CHECK(pocket_ui_set_event_handler(&tree,mbs.root,handler,&modal_trace)==POCKET_UI_OK);
    CHECK(pocket_overlay_present(&overlays,&(PocketOverlaySpec){
        .id=99,.kind=POCKET_OVERLAY_MODAL,.root=modal,.owns_root=1,.captures_input=1,.dismiss_on_back=1
    })==POCKET_OVERLAY_OK);

    /* Existing focus must not route Accept through a modal to the background. */
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_NO_TARGET);
    CHECK(focus_trace.key==1&&focus_trace.focus_lost==1);
    CHECK(pocket_interaction_snapshot(&interaction,&state)==POCKET_INTERACTION_OK);
    CHECK(!pocket_ui_handle_valid(state.focused));
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_FOCUS_REJECTED);

    PocketComponentHandle modal_field=add(&components,POCKET_COMPONENT_TEXT_FIELD,modal);
    CHECK(absolute(&components,modal_field,10,120,180,40)==0);
    CHECK(pocket_layout_run(&layout,ms.root,320,240)==POCKET_UI_OK);
    PocketComponentSnapshot mfs;
    CHECK(pocket_component_snapshot(&components,modal_field,&mfs)==POCKET_COMPONENT_OK);
    Trace modal_focus={0};
    CHECK(pocket_ui_set_event_handler(&tree,mfs.root,handler,&modal_focus)==POCKET_UI_OK);
    CHECK(pocket_interaction_focus(&interaction,mfs.root)==POCKET_INTERACTION_OK);
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_OK);
    CHECK(modal_focus.key==1&&focus_trace.key==1);
    CHECK(pocket_interaction_clear_focus(&interaction)==POCKET_INTERACTION_OK);

    PocketPointerEvent modal_down={3,POCKET_POINTER_DOWN,20,20,60};
    CHECK(pocket_interaction_pointer(&interaction,&modal_down)==POCKET_INTERACTION_OK);
    CHECK(modal_trace.down==1&&background.down==1);
    PocketPointerEvent modal_up={3,POCKET_POINTER_UP,20,20,70};
    CHECK(pocket_interaction_pointer(&interaction,&modal_up)==POCKET_INTERACTION_OK);
    CHECK(pocket_overlay_dismiss(&overlays,99)==POCKET_OVERLAY_OK);

    PocketPointerEvent p4={4,POCKET_POINTER_DOWN,20,20,80};
    CHECK(pocket_interaction_pointer(&interaction,&p4)==POCKET_INTERACTION_OK);
    pocket_interaction_cancel_all(&interaction,90);
    CHECK(background.cancel==1);
    CHECK(pocket_interaction_snapshot(&interaction,&state)==POCKET_INTERACTION_OK&&state.active_pointers==0);

    /* An enabled field under a hidden/disabled ancestor is not focus eligible. */
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_OK);
    CHECK(pocket_component_set_disabled(&components,scene,1)==POCKET_COMPONENT_OK);
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_NO_TARGET);
    CHECK(focus_trace.key==1);
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_FOCUS_REJECTED);
    CHECK(pocket_component_set_disabled(&components,scene,0)==POCKET_COMPONENT_OK);
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_OK);
    CHECK(pocket_component_set_visible(&components,scene,0)==POCKET_COMPONENT_OK);
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_NO_TARGET);
    CHECK(focus_trace.key==1);
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_FOCUS_REJECTED);
    CHECK(pocket_component_set_visible(&components,scene,1)==POCKET_COMPONENT_OK);

    /* Mounted nodes belonging to a different page cannot receive focus. */
    PocketComponentHandle other=add(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0});
    PocketComponentHandle other_field=add(&components,POCKET_COMPONENT_TEXT_FIELD,other);
    PocketComponentSnapshot ofs;
    CHECK(pocket_component_snapshot(&components,other_field,&ofs)==POCKET_COMPONENT_OK);
    CHECK(pocket_interaction_focus(&interaction,ofs.root)==POCKET_INTERACTION_FOCUS_REJECTED);
    CHECK(pocket_component_destroy(&components,other)==POCKET_COMPONENT_OK);

    CHECK(pocket_interaction_set_focus_scope(&interaction,bs.root)==POCKET_INTERACTION_OK);
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_FOCUS_REJECTED);
    CHECK(pocket_interaction_set_focus_scope(&interaction,(PocketUiHandle){0})==POCKET_INTERACTION_OK);
    CHECK(pocket_interaction_focus(&interaction,fs.root)==POCKET_INTERACTION_OK);
    CHECK(pocket_component_destroy(&components,field)==POCKET_COMPONENT_OK);
    CHECK(pocket_interaction_key(&interaction,POCKET_KEY_ACTION_ACCEPT)==POCKET_INTERACTION_STALE_HANDLE);

    pocket_interaction_dispose(&interaction);
    pocket_overlay_dispose(&overlays);
    CHECK(pocket_component_destroy(&components,scene)==POCKET_COMPONENT_OK);
    pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
    puts("INTERACTION_OK");
    return 0;
}
