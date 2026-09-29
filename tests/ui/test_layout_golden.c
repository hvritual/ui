#include "hosts/linux/ui/layout.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"GOLDEN_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
static PocketUiHandle add(PocketUiTree *t,PocketUiHandle p,PocketUiNodeType type){
 PocketUiHandle h={0};if(pocket_ui_create(t,type,p,&h)!=POCKET_UI_OK||pocket_ui_mount(t,h)!=POCKET_UI_OK)return (PocketUiHandle){0};return h;
}
static PocketLayoutSpec fixed_h(int h){
 PocketLayoutSpec s=pocket_layout_spec_default();s.height=(PocketLength){POCKET_LENGTH_PX,h};return s;
}
static void out_rect(const PocketLayoutResult *r){printf("[%d,%d,%d,%d]",r->geometry.x,r->geometry.y,r->geometry.width,r->geometry.height);}
int main(int argc,char **argv){
 if(argc!=2) return 2;
 int height=atoi(argv[1]);
 if(height!=600&&height!=800) return 2;
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=16,.update_queue_capacity=16,.update_budget=8};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketUiHandle root=add(&tree,(PocketUiHandle){0},POCKET_UI_CONTAINER),header=add(&tree,root,POCKET_UI_CONTAINER),body=add(&tree,root,POCKET_UI_CONTAINER),footer=add(&tree,root,POCKET_UI_CONTAINER);CHECK(pocket_ui_handle_valid(footer));
 PocketUiHandle cards[6];for(int i=0;i<6;i++){cards[i]=add(&tree,body,POCKET_UI_COMPONENT);CHECK(pocket_ui_handle_valid(cards[i]));}
 PocketLayoutContext l={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=32,.safe_area={24,20,24,height==600?20:32}};CHECK(pocket_layout_init(&l,&lc)==POCKET_UI_OK);
 PocketLayoutSpec rs=pocket_layout_spec_default();rs.mode=POCKET_LAYOUT_COLUMN;rs.padding=(PocketLayoutInsets){16,16,16,16};rs.gap=12;rs.align_items=POCKET_ALIGN_STRETCH;CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
 PocketLayoutSpec hs=fixed_h(72);CHECK(pocket_layout_set(&l,header,&hs)==POCKET_UI_OK);
 PocketLayoutSpec bs=pocket_layout_spec_default();bs.mode=POCKET_LAYOUT_GRID;bs.height.kind=POCKET_LENGTH_FILL;bs.grid_columns=3;bs.gap=8;bs.padding=(PocketLayoutInsets){8,8,8,8};bs.align_items=POCKET_ALIGN_STRETCH;CHECK(pocket_layout_set(&l,body,&bs)==POCKET_UI_OK);
 PocketLayoutSpec fs=fixed_h(56);CHECK(pocket_layout_set(&l,footer,&fs)==POCKET_UI_OK);
 PocketLayoutSpec cs=fixed_h(96);for(int i=0;i<6;i++)CHECK(pocket_layout_set(&l,cards[i],&cs)==POCKET_UI_OK);
 CHECK(pocket_layout_run(&l,root,1024,height)==POCKET_UI_OK);
 PocketLayoutResult rr,hr,br,fr,cr[6];CHECK(pocket_layout_result(&l,root,&rr)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,header,&hr)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,body,&br)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,footer,&fr)==POCKET_UI_OK);for(int i=0;i<6;i++)CHECK(pocket_layout_result(&l,cards[i],&cr[i])==POCKET_UI_OK);
 printf("{\"profile\":\"1024x%d\",\"root\":",height);out_rect(&rr);printf(",\"header\":");out_rect(&hr);printf(",\"body\":");out_rect(&br);printf(",\"footer\":");out_rect(&fr);printf(",\"cards\":[");for(int i=0;i<6;i++){if(i)putchar(',');out_rect(&cr[i]);}printf("]}\n");
 pocket_layout_dispose(&l);pocket_ui_tree_dispose(&tree);return 0;
}
