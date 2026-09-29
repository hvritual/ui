#include "hosts/linux/input/interaction_bridge.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"INTERACTION_BRIDGE_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct {int down,move,up,cancel;} Trace;
static PocketUiEventAction event(void *context,PocketUiEvent *e){
 Trace *t=context;
 if(e->type==POCKET_UI_EVENT_POINTER_DOWN)t->down++;
 else if(e->type==POCKET_UI_EVENT_POINTER_MOVE)t->move++;
 else if(e->type==POCKET_UI_EVENT_POINTER_UP)t->up++;
 else if(e->type==POCKET_UI_EVENT_POINTER_CANCEL)t->cancel++;
 return POCKET_UI_EVENT_CONTINUE;
}
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=8,.update_queue_capacity=8,.update_budget=4};
 CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=16};
 CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
 PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=16};
 CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);

 PocketComponentHandle scene={0},button={0};
 CHECK(pocket_component_create(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},NULL,&scene)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_create(&components,POCKET_COMPONENT_BUTTON,scene,NULL,&button)==POCKET_COMPONENT_OK);
 PocketLayoutSpec root=pocket_layout_spec_default();root.mode=POCKET_LAYOUT_ABSOLUTE;
 CHECK(pocket_component_set_layout(&components,scene,&root)==POCKET_COMPONENT_OK);
 PocketLayoutSpec box=pocket_layout_spec_default();box.width=(PocketLength){POCKET_LENGTH_PX,200};
 box.height=(PocketLength){POCKET_LENGTH_PX,100};box.offset_x=(PocketLength){POCKET_LENGTH_PX,0};
 box.offset_y=(PocketLength){POCKET_LENGTH_PX,0};
 CHECK(pocket_component_set_layout(&components,button,&box)==POCKET_COMPONENT_OK);
 PocketComponentSnapshot scene_snapshot,button_snapshot;
 CHECK(pocket_component_snapshot(&components,scene,&scene_snapshot)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_snapshot(&components,button,&button_snapshot)==POCKET_COMPONENT_OK);
 CHECK(pocket_layout_run(&layout,scene_snapshot.root,320,240)==POCKET_UI_OK);
 Trace trace={0};CHECK(pocket_ui_set_event_handler(&tree,button_snapshot.root,event,&trace)==POCKET_UI_OK);

 PocketInteractionRuntime interaction={0};
 PocketInteractionConfig ic={.tree=&tree,.layout=&layout,.components=&components,.scene_root=scene_snapshot.root};
 CHECK(pocket_interaction_init(&interaction,&ic)==POCKET_INTERACTION_OK);
 PocketInputInteractionBridge bridge;CHECK(pocket_input_interaction_bridge_init(&bridge,&interaction));

 InputFrame frame;memset(&frame,0,sizeof(frame));
 frame.contact_count=1;frame.contacts[0]=(InputContact){0,20,20};
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,100000000ULL));
 frame.contacts[0].x=25;
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,150000000ULL));
 frame.contact_count=0;
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,200000000ULL));
 CHECK(trace.down==1&&trace.move==1&&trace.up==1);

 frame.contact_count=1;frame.contacts[0]=(InputContact){11,30,30};
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,300000000ULL));
 frame.syn_dropped=1;frame.suppressed=1;
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,350000000ULL));
 CHECK(trace.cancel==1&&bridge.cancels==1);

 frame.syn_dropped=0;frame.suppressed=0;frame.contact_count=1;
 frame.contacts[0]=(InputContact){12,300,200};
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,400000000ULL));
 CHECK(bridge.ignored_no_target==1);
 frame.contact_count=0;
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,420000000ULL));

 frame.contact_count=1;frame.contacts[0]=(InputContact){13,40,40};
 CHECK(pocket_input_interaction_bridge_frame(&bridge,&frame,500000000ULL));
 pocket_input_interaction_bridge_disconnect(&bridge,550000000ULL);
 CHECK(trace.cancel==2&&bridge.cancels==2);

 pocket_interaction_dispose(&interaction);
 CHECK(pocket_component_destroy(&components,scene)==POCKET_COMPONENT_OK);
 pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
 puts("INTERACTION_BRIDGE_OK");
 return 0;
}
