#include "hosts/linux/ui/component.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"COMPONENT_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct{int activate,value,toggle,focus,submit;int last;} Events;
static void event(void *ctx,PocketComponentHandle h,const PocketComponentEvent *e){
 (void)h;Events *v=ctx;v->last=e->value;
 if(e->type==POCKET_COMPONENT_EVENT_ACTIVATE)v->activate++;
 else if(e->type==POCKET_COMPONENT_EVENT_VALUE_CHANGED)v->value++;
 else if(e->type==POCKET_COMPONENT_EVENT_TOGGLE_CHANGED)v->toggle++;
 else if(e->type==POCKET_COMPONENT_EVENT_FOCUS_REQUEST)v->focus++;
 else if(e->type==POCKET_COMPONENT_EVENT_SUBMIT)v->submit++;
}
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=16,.update_queue_capacity=16,.update_budget=8};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=128};CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
 PocketComponentRuntime rt={0};PocketComponentRuntimeConfig rc={.tree=&tree,.layout=&layout,.capacity=64};CHECK(pocket_component_runtime_init(&rt,&rc)==POCKET_COMPONENT_OK);
 PocketComponentHandle root={0};CHECK(pocket_component_create(&rt,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},NULL,&root)==POCKET_COMPONENT_OK);
 PocketComponentHandle all[POCKET_COMPONENT_KIND_COUNT]={0};
 for(int k=POCKET_COMPONENT_TEXT;k<POCKET_COMPONENT_KIND_COUNT;k++){
   PocketComponentProps p=pocket_component_props_default((PocketComponentKind)k);
   if(k==POCKET_COMPONENT_RADIO)p.group_id=1;
   if(k==POCKET_COMPONENT_TEXT_FIELD)p.text_session_id=99;
   CHECK(pocket_component_create(&rt,(PocketComponentKind)k,root,&p,&all[k])==POCKET_COMPONENT_OK);
   PocketComponentSnapshot s;CHECK(pocket_component_snapshot(&rt,all[k],&s)==POCKET_COMPONENT_OK);
   CHECK(s.kind==(PocketComponentKind)k&&s.props.style_ref==pocket_component_default_style_ref((PocketComponentKind)k));
 }
 CHECK(pocket_component_live_count(&rt)==POCKET_COMPONENT_KIND_COUNT);

 Events e={0};PocketComponentHandle button=all[POCKET_COMPONENT_BUTTON];
 CHECK(pocket_component_set_event_handler(&rt,button,event,&e)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,button)==POCKET_COMPONENT_OK&&e.activate==1);
 CHECK(pocket_component_set_states(&rt,button,POCKET_STATE_DISABLED)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,button)==POCKET_COMPONENT_DISABLED);
 CHECK(pocket_component_set_states(&rt,button,0)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,button)==POCKET_COMPONENT_OK&&e.activate==2);

 PocketComponentHandle toggle=all[POCKET_COMPONENT_TOGGLE];CHECK(pocket_component_set_event_handler(&rt,toggle,event,&e)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,toggle)==POCKET_COMPONENT_OK&&e.toggle==1);
 PocketComponentSnapshot snap;CHECK(pocket_component_snapshot(&rt,toggle,&snap)==POCKET_COMPONENT_OK&&(snap.props.states&POCKET_STATE_CHECKED));

 PocketComponentHandle slider=all[POCKET_COMPONENT_SLIDER];CHECK(pocket_component_set_event_handler(&rt,slider,event,&e)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_set_value(&rt,slider,73)==POCKET_COMPONENT_OK&&e.value==1&&e.last==73);
 CHECK(pocket_component_set_value(&rt,slider,101)==POCKET_COMPONENT_INVALID_ARGUMENT);

 PocketComponentHandle field=all[POCKET_COMPONENT_TEXT_FIELD];CHECK(pocket_component_set_event_handler(&rt,field,event,&e)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,field)==POCKET_COMPONENT_OK&&e.focus==1);
 CHECK(pocket_component_submit(&rt,field)==POCKET_COMPONENT_OK&&e.submit==1);

 PocketComponentProps rp=pocket_component_props_default(POCKET_COMPONENT_RADIO);rp.group_id=7;
 PocketComponentHandle r1,r2;CHECK(pocket_component_create(&rt,POCKET_COMPONENT_RADIO,root,&rp,&r1)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_create(&rt,POCKET_COMPONENT_RADIO,root,&rp,&r2)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_activate(&rt,r1)==POCKET_COMPONENT_OK);CHECK(pocket_component_activate(&rt,r2)==POCKET_COMPONENT_OK);
 PocketComponentSnapshot s1,s2;CHECK(pocket_component_snapshot(&rt,r1,&s1)==POCKET_COMPONENT_OK);CHECK(pocket_component_snapshot(&rt,r2,&s2)==POCKET_COMPONENT_OK);
 CHECK(!(s1.props.states&POCKET_STATE_CHECKED)&&(s2.props.states&POCKET_STATE_CHECKED));

 CHECK(pocket_component_destroy(&rt,root)==POCKET_COMPONENT_OK);
 CHECK(pocket_component_live_count(&rt)==0);CHECK(pocket_component_snapshot(&rt,button,&snap)==POCKET_COMPONENT_STALE_HANDLE);
 pocket_component_runtime_dispose(&rt);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
 puts("COMPONENTS_OK");return 0;
}
