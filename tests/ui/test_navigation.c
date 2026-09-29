#include "hosts/linux/ui/navigation.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"NAV_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct{int timer,task,input;int life;} Trace;
static void cancel(void *ctx,PocketNavigationResourceKind kind){Trace *t=ctx;if(kind==POCKET_NAV_RESOURCE_TIMER)t->timer++;else if(kind==POCKET_NAV_RESOURCE_TASK)t->task++;else if(kind==POCKET_NAV_RESOURCE_INPUT_CAPTURE)t->input++;}
static void life(void *ctx,uint64_t route,PocketNavigationLifecycle event,PocketNavigationTransition transition){(void)route;(void)event;(void)transition;((Trace *)ctx)->life++;}
static PocketComponentHandle page(PocketComponentRuntime *rt){PocketComponentHandle h={0};if(pocket_component_create(rt,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},NULL,&h)!=POCKET_COMPONENT_OK)return (PocketComponentHandle){0};return h;}
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=8,.update_queue_capacity=8,.update_budget=4};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=32};CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
 PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=32};CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);
 Trace trace={0};PocketNavigationStack nav={0};PocketNavigationConfig nc={.components=&components,.capacity=8,.lifecycle=life,.lifecycle_context=&trace};CHECK(pocket_navigation_init(&nav,&nc)==POCKET_NAV_OK);
 PocketComponentHandle home=page(&components),detail=page(&components),making=page(&components),success=page(&components);
 CHECK(pocket_component_handle_valid(home)&&pocket_component_handle_valid(success));
 CHECK(pocket_navigation_push(&nav,&(PocketNavigationPage){1,home,1})==POCKET_NAV_OK);
 CHECK(pocket_navigation_push(&nav,&(PocketNavigationPage){2,detail,1})==POCKET_NAV_OK);
 CHECK(pocket_navigation_register_cancel(&nav,2,POCKET_NAV_RESOURCE_TIMER,cancel,&trace)==POCKET_NAV_OK);
 CHECK(pocket_navigation_register_cancel(&nav,2,POCKET_NAV_RESOURCE_TASK,cancel,&trace)==POCKET_NAV_OK);
 CHECK(pocket_navigation_register_cancel(&nav,2,POCKET_NAV_RESOURCE_INPUT_CAPTURE,cancel,&trace)==POCKET_NAV_OK);
 CHECK(pocket_navigation_replace(&nav,&(PocketNavigationPage){3,making,1})==POCKET_NAV_OK);
 CHECK(trace.timer==1&&trace.task==1&&trace.input==1&&pocket_navigation_count(&nav)==2);
 CHECK(pocket_navigation_replace(&nav,&(PocketNavigationPage){4,success,1})==POCKET_NAV_OK);
 PocketNavigationPage top;CHECK(pocket_navigation_top(&nav,&top)==POCKET_NAV_OK&&top.route_id==4);
 int consumed=0;CHECK(pocket_navigation_back(&nav,&consumed)==POCKET_NAV_OK&&consumed==1);
 CHECK(pocket_navigation_top(&nav,&top)==POCKET_NAV_OK&&top.route_id==1);
 CHECK(pocket_navigation_back(&nav,&consumed)==POCKET_NAV_ROOT);
 CHECK(pocket_navigation_reset(&nav,&(PocketNavigationPage){1,home,0})==POCKET_NAV_DUPLICATE_ROUTE);
 PocketComponentHandle fresh=page(&components);CHECK(pocket_component_handle_valid(fresh));
 CHECK(pocket_navigation_reset(&nav,&(PocketNavigationPage){10,fresh,1})==POCKET_NAV_OK);
 CHECK(pocket_navigation_count(&nav)==1&&trace.life>0);
 pocket_navigation_dispose(&nav);CHECK(pocket_component_live_count(&components)==0);
 pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
 puts("NAVIGATION_OK");return 0;
}
