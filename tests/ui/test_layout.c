#include "hosts/linux/ui/layout.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"LAYOUT_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

static int measure(void *ctx,PocketUiHandle node,int32_t mw,int32_t mh,int32_t *w,int32_t *h){
    (void)ctx;(void)mw;(void)mh;
    if(node.slot%2){*w=42;*h=18;}else{*w=64;*h=24;}
    return 1;
}
static PocketUiHandle add(PocketUiTree *tree,PocketUiHandle parent,PocketUiNodeType type){
    PocketUiHandle h={0};
    if(pocket_ui_create(tree,type,parent,&h)!=POCKET_UI_OK)return (PocketUiHandle){0};
    if(pocket_ui_mount(tree,h)!=POCKET_UI_OK)return (PocketUiHandle){0};
    return h;
}
static PocketLayoutSpec leaf_px(int w,int h){
    PocketLayoutSpec s=pocket_layout_spec_default();s.width=(PocketLength){POCKET_LENGTH_PX,w};
    s.height=(PocketLength){POCKET_LENGTH_PX,h};return s;
}
int main(void){
    PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=8,.update_queue_capacity=8,.update_budget=4};
    CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
    PocketUiHandle root=add(&tree,(PocketUiHandle){0},POCKET_UI_CONTAINER);CHECK(pocket_ui_handle_valid(root));
    PocketUiHandle a=add(&tree,root,POCKET_UI_COMPONENT),b=add(&tree,root,POCKET_UI_COMPONENT),c=add(&tree,root,POCKET_UI_COMPONENT);
    CHECK(pocket_ui_handle_valid(a)&&pocket_ui_handle_valid(b)&&pocket_ui_handle_valid(c));

    PocketLayoutContext l={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=32,.measure=measure};
    CHECK(pocket_layout_init(&l,&lc)==POCKET_UI_OK);
    PocketLayoutSpec rs=pocket_layout_spec_default();rs.mode=POCKET_LAYOUT_ROW;rs.gap=10;rs.align_items=POCKET_ALIGN_CENTER;
    rs.padding=(PocketLayoutInsets){10,10,10,10};CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    PocketLayoutSpec as=leaf_px(100,40);as.grow=1;CHECK(pocket_layout_set(&l,a,&as)==POCKET_UI_OK);
    PocketLayoutSpec bs=leaf_px(100,60);bs.grow=2;CHECK(pocket_layout_set(&l,b,&bs)==POCKET_UI_OK);
    PocketLayoutSpec cs=leaf_px(100,20);CHECK(pocket_layout_set(&l,c,&cs)==POCKET_UI_OK);
    CHECK(pocket_layout_run(&l,root,400,200)==POCKET_UI_OK);
    PocketLayoutResult ar,br,cr;CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);
    CHECK(pocket_layout_result(&l,b,&br)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,c,&cr)==POCKET_UI_OK);
    CHECK(ar.geometry.x==10 && ar.geometry.width==120 && ar.geometry.y==80);
    CHECK(br.geometry.x==140 && br.geometry.width==140 && br.geometry.y==70);
    CHECK(cr.geometry.x==290 && cr.geometry.width==100 && cr.geometry.y==90);

    rs.mode=POCKET_LAYOUT_COLUMN;rs.wrap=1;rs.gap=5;rs.padding=(PocketLayoutInsets){0,0,0,0};
    CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    as=leaf_px(40,70);bs=leaf_px(50,70);cs=leaf_px(60,70);
    CHECK(pocket_layout_set(&l,a,&as)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,b,&bs)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,c,&cs)==POCKET_UI_OK);
    CHECK(pocket_layout_run(&l,root,200,150)==POCKET_UI_OK);
    CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,b,&br)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,c,&cr)==POCKET_UI_OK);
    CHECK(ar.geometry.x==5 && ar.geometry.y==0);
    CHECK(br.geometry.x==0 && br.geometry.y==75);
    CHECK(cr.geometry.x==55 && cr.geometry.y==0);

    rs.mode=POCKET_LAYOUT_STACK;rs.align_items=POCKET_ALIGN_CENTER;rs.wrap=0;
    CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    PocketLayoutSpec content=pocket_layout_spec_default();content.width.kind=POCKET_LENGTH_CONTENT;content.height.kind=POCKET_LENGTH_CONTENT;
    CHECK(pocket_layout_set(&l,a,&content)==POCKET_UI_OK);
    CHECK(pocket_layout_run(&l,root,200,100)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);
    CHECK(ar.geometry.width==64 && ar.geometry.height==24 && ar.geometry.x==68 && ar.geometry.y==38);

    rs.mode=POCKET_LAYOUT_ABSOLUTE;rs.overflow=POCKET_OVERFLOW_CLIP;CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    PocketLayoutSpec abs=leaf_px(100,80);abs.offset_x=(PocketLength){POCKET_LENGTH_PERCENT,8000};abs.offset_y=(PocketLength){POCKET_LENGTH_PX,50};
    CHECK(pocket_layout_set(&l,a,&abs)==POCKET_UI_OK);CHECK(pocket_layout_run(&l,root,200,100)==POCKET_UI_OK);
    CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);CHECK(ar.geometry.x==160 && ar.geometry.y==50);
    CHECK(ar.clip.width==40 && ar.clip.height==50);

    rs.mode=POCKET_LAYOUT_GRID;rs.grid_columns=2;rs.gap=8;rs.align_items=POCKET_ALIGN_STRETCH;CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    as=pocket_layout_spec_default();as.height=(PocketLength){POCKET_LENGTH_PX,30};
    bs=as;cs=as;CHECK(pocket_layout_set(&l,a,&as)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,b,&bs)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,c,&cs)==POCKET_UI_OK);
    CHECK(pocket_layout_run(&l,root,208,100)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);
    CHECK(pocket_layout_result(&l,b,&br)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,c,&cr)==POCKET_UI_OK);
    CHECK(ar.geometry.x==0 && ar.geometry.width==100 && br.geometry.x==108 && br.geometry.width==100 && cr.geometry.y==38);

    /* Flex grow/shrink and stretch cannot violate min/max. */
    rs.mode=POCKET_LAYOUT_ROW;rs.align_items=POCKET_ALIGN_STRETCH;rs.gap=0;rs.padding=(PocketLayoutInsets){0,0,0,0};
    CHECK(pocket_layout_set(&l,root,&rs)==POCKET_UI_OK);
    as=leaf_px(50,10);as.grow=1;as.max_width=60;as.max_height=20;
    bs=leaf_px(100,10);bs.shrink=1;bs.min_width=90;bs.max_height=20;
    cs=leaf_px(50,10);cs.grow=1;cs.max_width=60;cs.max_height=20;
    CHECK(pocket_layout_set(&l,a,&as)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,b,&bs)==POCKET_UI_OK);CHECK(pocket_layout_set(&l,c,&cs)==POCKET_UI_OK);
    CHECK(pocket_layout_run(&l,root,300,80)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,b,&br)==POCKET_UI_OK);
    CHECK(ar.geometry.width==60&&ar.geometry.height==10&&br.geometry.width==100);
    CHECK(pocket_layout_run(&l,root,180,80)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,b,&br)==POCKET_UI_OK);
    CHECK(br.geometry.width>=90);

    PocketLayoutSpec ratio=leaf_px(80,10);ratio.height=(PocketLength){POCKET_LENGTH_AUTO,0};ratio.aspect_num=2;ratio.aspect_den=1;
    CHECK(pocket_layout_set(&l,a,&ratio)==POCKET_UI_OK);CHECK(pocket_layout_run(&l,root,300,80)==POCKET_UI_OK);CHECK(pocket_layout_result(&l,a,&ar)==POCKET_UI_OK);
    CHECK(ar.geometry.height==40);

    PocketLayoutSpec bad=pocket_layout_spec_default();bad.width=(PocketLength){POCKET_LENGTH_PERCENT,10001};
    CHECK(pocket_layout_set(&l,a,&bad)==POCKET_UI_INVALID_ARGUMENT);
    bad=pocket_layout_spec_default();bad.offset_x=(PocketLength){POCKET_LENGTH_AUTO,0};
    CHECK(pocket_layout_set(&l,a,&bad)==POCKET_UI_INVALID_ARGUMENT);
    lc.safe_area=(PocketLayoutInsets){101,0,100,0}; /* existing context unchanged */
    pocket_layout_dispose(&l);pocket_ui_tree_dispose(&tree);

    /* Layout records are generation-scoped but must recycle stale history. */
    PocketUiTree churn={0};PocketUiTreeConfig churn_tc={.initial_capacity=2,.update_queue_capacity=4,.update_budget=2};
    CHECK(pocket_ui_tree_init(&churn,&churn_tc)==POCKET_UI_OK);
    PocketUiHandle churn_root=add(&churn,(PocketUiHandle){0},POCKET_UI_CONTAINER);CHECK(pocket_ui_handle_valid(churn_root));
    PocketLayoutContext churn_layout={0};PocketLayoutConfig churn_cfg={.tree=&churn,.record_capacity=2};
    CHECK(pocket_layout_init(&churn_layout,&churn_cfg)==POCKET_UI_OK);
    PocketLayoutSpec churn_root_spec=pocket_layout_spec_default();churn_root_spec.mode=POCKET_LAYOUT_STACK;churn_root_spec.align_items=POCKET_ALIGN_START;
    CHECK(pocket_layout_set(&churn_layout,churn_root,&churn_root_spec)==POCKET_UI_OK);
    PocketUiHandle old={0};
    for(int i=0;i<32;i++){
        PocketUiHandle child=add(&churn,churn_root,POCKET_UI_COMPONENT);CHECK(pocket_ui_handle_valid(child));
        PocketLayoutSpec child_spec=leaf_px(10,10);CHECK(pocket_layout_set(&churn_layout,child,&child_spec)==POCKET_UI_OK);
        CHECK(pocket_layout_run(&churn_layout,churn_root,100,100)==POCKET_UI_OK);
        PocketLayoutResult result;CHECK(pocket_layout_result(&churn_layout,child,&result)==POCKET_UI_OK);
        old=child;CHECK(pocket_ui_destroy(&churn,child)==POCKET_UI_OK);
        CHECK(pocket_layout_result(&churn_layout,old,&result)==POCKET_UI_STALE_HANDLE);
    }
    pocket_layout_dispose(&churn_layout);pocket_ui_tree_dispose(&churn);
    puts("LAYOUT_OK");
    return 0;
}
