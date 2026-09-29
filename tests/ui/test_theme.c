#include "hosts/linux/ui/style.h"
#include "hosts/linux/ui/object.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"THEME_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=4,.update_queue_capacity=4,.update_budget=2};CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
 PocketUiHandle root={0},node={0};CHECK(pocket_ui_create(&tree,POCKET_UI_CONTAINER,(PocketUiHandle){0},&root)==POCKET_UI_OK);CHECK(pocket_ui_mount(&tree,root)==POCKET_UI_OK);
 CHECK(pocket_ui_create(&tree,POCKET_UI_COMPONENT,root,&node)==POCKET_UI_OK);CHECK(pocket_ui_mount(&tree,node)==POCKET_UI_OK);
 PocketUiSnapshot before;CHECK(pocket_ui_snapshot(&tree,node,&before)==POCKET_UI_OK);PocketUiProperties p=before.properties;p.style_ref=10;
 CHECK(pocket_ui_update(&tree,node,POCKET_UI_PROP_STYLE_REF,&p)==POCKET_UI_OK);
 PocketStyleRuntime rt={0};CHECK(pocket_style_runtime_init(&rt,NULL)==POCKET_STYLE_OK);
 PocketThemeToken a[]={{1,0xffffff}},b[]={{1,0x111111}};PocketStyle base={0};
 PocketThemeDefinition ta={1,base,a,1},tb={2,base,b,1};CHECK(pocket_style_add_theme(&rt,&ta)==POCKET_STYLE_OK);CHECK(pocket_style_add_theme(&rt,&tb)==POCKET_STYLE_OK);
 PocketStyle s={0};s.set_mask=POCKET_STYLE_BIT(POCKET_STYLE_BACKGROUND);s.fields[POCKET_STYLE_BACKGROUND]=pocket_style_token(1);
 CHECK(pocket_style_add_rule(&rt,&(PocketStyleRule){10,0,1,s})==POCKET_STYLE_OK);
 PocketResolvedStyle first,second;CHECK(pocket_style_resolve(&rt,10,0,NULL,&first)==POCKET_STYLE_OK);uint64_t e1=pocket_style_theme_epoch(&rt);
 CHECK(pocket_style_set_theme(&rt,2)==POCKET_STYLE_OK);CHECK(pocket_style_theme_epoch(&rt)==e1+1);CHECK(pocket_style_resolve(&rt,10,0,NULL,&second)==POCKET_STYLE_OK);
 PocketUiSnapshot after;CHECK(pocket_ui_snapshot(&tree,node,&after)==POCKET_UI_OK);
 CHECK(after.handle.slot==before.handle.slot&&after.handle.generation==before.handle.generation&&after.stable_id==before.stable_id);
 CHECK(first.fields[POCKET_STYLE_BACKGROUND]==0xffffff&&second.fields[POCKET_STYLE_BACKGROUND]==0x111111);
 CHECK(pocket_style_set_theme(&rt,99)==POCKET_STYLE_NOT_FOUND);pocket_style_runtime_dispose(&rt);pocket_ui_tree_dispose(&tree);
 puts("THEME_OK");return 0;
}
