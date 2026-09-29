#include "hosts/linux/ui/overlay.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"OVERLAY_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static PocketComponentHandle root(PocketComponentRuntime *rt,PocketComponentKind kind){PocketComponentHandle h={0};if(pocket_component_create(rt,kind,(PocketComponentHandle){0},NULL,&h)!=POCKET_COMPONENT_OK)return (PocketComponentHandle){0};return h;}
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=8,.update_queue_capacity=8,.update_budget=4};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=32};CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
 PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=32};CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);
 PocketOverlayManager overlays={0};PocketOverlayConfig oc={.components=&components,.capacity=8};CHECK(pocket_overlay_init(&overlays,&oc)==POCKET_OVERLAY_OK);
 PocketComponentHandle toast=root(&components,POCKET_COMPONENT_TOAST),modal=root(&components,POCKET_COMPONENT_MODAL),keyboard=root(&components,POCKET_COMPONENT_VIEW);
 CHECK(pocket_overlay_present(&overlays,&(PocketOverlaySpec){1,10,POCKET_OVERLAY_TOAST,toast,0,1,0,0,1})==POCKET_OVERLAY_OK);
 CHECK(pocket_overlay_present(&overlays,&(PocketOverlaySpec){2,10,POCKET_OVERLAY_MODAL,modal,100,1,1,1,1})==POCKET_OVERLAY_OK);
 CHECK(pocket_overlay_present(&overlays,&(PocketOverlaySpec){3,11,POCKET_OVERLAY_KEYBOARD,keyboard,200,1,1,1,1})==POCKET_OVERLAY_OK);
 PocketOverlaySnapshot top;CHECK(pocket_overlay_top(&overlays,&top)==POCKET_OVERLAY_OK&&top.spec.id==3);
 CHECK(pocket_overlay_blocks_background(&overlays)&&pocket_overlay_focus_token(&overlays)==200);
 int consumed=0;CHECK(pocket_overlay_back(&overlays,&consumed)==POCKET_OVERLAY_OK&&consumed==1);
 CHECK(pocket_overlay_focus_token(&overlays)==100);CHECK(pocket_overlay_top(&overlays,&top)==POCKET_OVERLAY_OK&&top.spec.id==2);
 CHECK(pocket_overlay_dismiss_owner(&overlays,10)==2&&pocket_overlay_count(&overlays)==0);
 CHECK(!pocket_overlay_blocks_background(&overlays));
 pocket_overlay_dispose(&overlays);CHECK(pocket_component_live_count(&components)==0);
 pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
 puts("OVERLAY_OK");return 0;
}
