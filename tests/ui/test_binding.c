#include "hosts/linux/ui/reactive.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"BINDING_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

int main(void) {
    PocketUiTree tree={0};
    PocketUiTreeConfig tc={.initial_capacity=8,.update_queue_capacity=8,.update_budget=4};
    CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
    PocketLayoutContext layout={0};
    PocketLayoutConfig lc={.tree=&tree,.record_capacity=32};
    CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
    PocketComponentRuntime components={0};
    PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=16};
    CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);

    PocketComponentHandle root={0},button={0},slider={0};
    CHECK(pocket_component_create(&components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},NULL,&root)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_create(&components,POCKET_COMPONENT_BUTTON,root,NULL,&button)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_create(&components,POCKET_COMPONENT_SLIDER,root,NULL,&slider)==POCKET_COMPONENT_OK);

    PocketReactiveRuntime reactive={0};
    PocketReactiveConfig rc={.node_capacity=16,.effect_capacity=0,.binding_capacity=16,
                             .default_flush_budget=32,.components=&components};
    CHECK(pocket_reactive_init(&reactive,&rc)==POCKET_REACTIVE_OK);
    PocketReactiveHandle text,style,visible,disabled,value;
    CHECK(pocket_reactive_signal(&reactive,pocket_value_u64(100),&text)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_signal(&reactive,pocket_value_u64(0x2222),&style)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_signal(&reactive,pocket_value_bool(1),&visible)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_signal(&reactive,pocket_value_bool(0),&disabled)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_signal(&reactive,pocket_value_i64(25),&value)==POCKET_REACTIVE_OK);

    PocketReactiveSubscription ids[5];
    CHECK(pocket_reactive_bind_component(&reactive,text,button,POCKET_BIND_TEXT_REF,&ids[0])==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_bind_component(&reactive,style,button,POCKET_BIND_STYLE_REF,&ids[1])==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_bind_component(&reactive,visible,button,POCKET_BIND_VISIBLE,&ids[2])==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_bind_component(&reactive,disabled,button,POCKET_BIND_DISABLED,&ids[3])==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_bind_component(&reactive,value,slider,POCKET_BIND_VALUE,&ids[4])==POCKET_REACTIVE_OK);
    PocketReactiveFlushStats stats;
    CHECK(pocket_reactive_flush(&reactive,0,&stats)==POCKET_REACTIVE_OK&&stats.binding_updates==5);
    PocketComponentSnapshot b,s;
    CHECK(pocket_component_snapshot(&components,button,&b)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_snapshot(&components,slider,&s)==POCKET_COMPONENT_OK);
    CHECK(b.props.text_ref==100&&b.props.style_ref==0x2222&&b.props.visible&&!b.props.disabled&&s.props.value==25);

    CHECK(pocket_reactive_batch_begin(&reactive)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&reactive,text,pocket_value_u64(200))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&reactive,visible,pocket_value_bool(0))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&reactive,disabled,pocket_value_bool(1))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&reactive,value,pocket_value_i64(80))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_batch_end(&reactive)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&reactive,0,&stats)==POCKET_REACTIVE_OK&&stats.binding_updates==4);
    CHECK(pocket_component_snapshot(&components,button,&b)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_snapshot(&components,slider,&s)==POCKET_COMPONENT_OK);
    CHECK(b.props.text_ref==200&&!b.props.visible&&b.props.disabled&&s.props.value==80);

    CHECK(pocket_component_destroy(&components,button)==POCKET_COMPONENT_OK);
    CHECK(pocket_reactive_set(&reactive,text,pocket_value_u64(300))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&reactive,0,&stats)==POCKET_REACTIVE_OK&&stats.binding_cleanups==4);
    CHECK(pocket_reactive_binding_count(&reactive)==1);
    CHECK(pocket_reactive_unbind(&reactive,ids[4])==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_binding_count(&reactive)==0);

    pocket_reactive_dispose(&reactive);
    CHECK(pocket_component_destroy(&components,root)==POCKET_COMPONENT_OK);
    pocket_component_runtime_dispose(&components);
    pocket_layout_dispose(&layout);
    pocket_ui_tree_dispose(&tree);
    puts("BINDING_OK");
    return 0;
}
