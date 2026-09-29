#include "hosts/linux/ui/navigation.h"
#include "hosts/linux/ui/overlay.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"COFFEE_COMPONENT_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static PocketComponentHandle make(PocketComponentRuntime *rt,PocketComponentKind kind,PocketComponentHandle parent,uint64_t text){
 PocketComponentProps p=pocket_component_props_default(kind);p.text_ref=text;PocketComponentHandle h={0};
 if(pocket_component_create(rt,kind,parent,&p,&h)!=POCKET_COMPONENT_OK)
  return (PocketComponentHandle){0};
 return h;
}
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=32,.update_queue_capacity=32,.update_budget=16};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=128};CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
 PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=128};CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);
 PocketOverlayManager overlays={0};PocketOverlayConfig oc={.components=&components,.capacity=16};CHECK(pocket_overlay_init(&overlays,&oc)==POCKET_OVERLAY_OK);
 PocketNavigationStack nav={0};PocketNavigationConfig nc={.components=&components,.capacity=8,.owner_cleanup=pocket_overlay_navigation_cleanup,.owner_cleanup_context=&overlays};CHECK(pocket_navigation_init(&nav,&nc)==POCKET_NAV_OK);

 PocketComponentHandle home=make(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0);CHECK(pocket_component_handle_valid(home));
 PocketComponentHandle list=make(&components,POCKET_COMPONENT_GRID,home,0);CHECK(pocket_component_handle_valid(list));
 for(int i=0;i<8;i++)CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_BUTTON,list,100+i)));
 CHECK(pocket_navigation_push(&nav,&(PocketNavigationPage){1,home,1})==POCKET_NAV_OK);

 PocketComponentHandle detail=make(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0);
 CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_IMAGE,detail,200)));
 CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_TEXT,detail,201)));
 CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_BUTTON,detail,202)));
 CHECK(pocket_navigation_push(&nav,&(PocketNavigationPage){2,detail,1})==POCKET_NAV_OK);

 PocketComponentHandle making=make(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0);
 PocketComponentHandle progress=make(&components,POCKET_COMPONENT_PROGRESS,making,300);CHECK(pocket_component_set_value(&components,progress,45)==POCKET_COMPONENT_OK);
 CHECK(pocket_navigation_replace(&nav,&(PocketNavigationPage){3,making,1})==POCKET_NAV_OK);
 PocketComponentHandle loading=make(&components,POCKET_COMPONENT_LOADING,(PocketComponentHandle){0},301);
 CHECK(pocket_overlay_present(&overlays,&(PocketOverlaySpec){33,3,POCKET_OVERLAY_LOADING,loading,0,1,1,0,0})==POCKET_OVERLAY_OK);
 CHECK(pocket_overlay_blocks_background(&overlays));

 PocketComponentHandle success=make(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},0);
 CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_TEXT,success,400)));
 CHECK(pocket_component_handle_valid(make(&components,POCKET_COMPONENT_BUTTON,success,401)));
 CHECK(pocket_navigation_replace(&nav,&(PocketNavigationPage){4,success,1})==POCKET_NAV_OK);
 CHECK(pocket_overlay_count(&overlays)==0);
 PocketNavigationPage top;CHECK(pocket_navigation_top(&nav,&top)==POCKET_NAV_OK&&top.route_id==4);
 int consumed=0;CHECK(pocket_navigation_back(&nav,&consumed)==POCKET_NAV_OK&&consumed);
 CHECK(pocket_navigation_top(&nav,&top)==POCKET_NAV_OK&&top.route_id==1);

 pocket_navigation_dispose(&nav);pocket_overlay_dispose(&overlays);
 CHECK(pocket_component_live_count(&components)==0);
 pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
 puts("COFFEE_COMPONENT_MIGRATION_OK flow=home-detail-making-success");return 0;
}
