#include "hosts/linux/ui/model.h"
#include <stdio.h>
#include <sys/resource.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"VIRTUAL_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct {
    uint32_t count;
    uint64_t base_key;
    uint64_t revision;
    uint64_t binds;
} Fixture;

static uint32_t model_count(void *context) {
    return ((Fixture *)context)->count;
}
static int model_key(void *context,uint32_t index,uint64_t *key) {
    Fixture *f=context;
    if(index>=f->count||!key)return 0;
    *key=f->base_key+index+1U;
    return 1;
}
static PocketComponentStatus model_bind(void *context,uint32_t index,uint64_t key,
                                        PocketComponentRuntime *components,
                                        PocketComponentHandle component) {
    Fixture *f=context;f->binds++;
    PocketComponentStatus status=pocket_component_set_text_ref(components,component,key+f->revision*100000U);
    if(status!=POCKET_COMPONENT_OK)return status;
    return pocket_component_set_resource_ref(components,component,1000U+index);
}
static PocketComponentHandle make_parent(PocketComponentRuntime *components,PocketComponentKind kind) {
    PocketComponentHandle parent={0};
    if(pocket_component_create(components,kind,(PocketComponentHandle){0},NULL,&parent)!=POCKET_COMPONENT_OK)
        return (PocketComponentHandle){0};
    return parent;
}
int main(void) {
    PocketUiTree tree={0};PocketUiTreeConfig tc={.initial_capacity=32,.update_queue_capacity=32,.update_budget=16};
    CHECK(pocket_ui_tree_init(&tree,&tc)==POCKET_UI_OK);
    PocketLayoutContext layout={0};PocketLayoutConfig lc={.tree=&tree,.record_capacity=128};
    CHECK(pocket_layout_init(&layout,&lc)==POCKET_UI_OK);
    PocketComponentRuntime components={0};PocketComponentRuntimeConfig cc={.tree=&tree,.layout=&layout,.capacity=128};
    CHECK(pocket_component_runtime_init(&components,&cc)==POCKET_COMPONENT_OK);

    Fixture drinks={100,1000,0,0};PocketComponentHandle list=make_parent(&components,POCKET_COMPONENT_LIST);
    CHECK(pocket_component_handle_valid(list));
    PocketVirtualCollection collection={0};
    PocketVirtualCollectionConfig vc={.components=&components,.parent=list,.item_kind=POCKET_COMPONENT_BUTTON,
        .overscan=2,.max_pool=16,.model={&drinks,model_count,model_key,model_bind}};
    CHECK(pocket_virtual_collection_init(&collection,&vc)==POCKET_MODEL_OK);
    CHECK(pocket_virtual_collection_set_window(&collection,0,8)==POCKET_MODEL_OK);
    PocketVirtualCollectionStats stats;CHECK(pocket_virtual_collection_stats(&collection,&stats)==POCKET_MODEL_OK);
    CHECK(stats.model_count==100&&stats.materialized_count==10&&stats.pool_size==10&&stats.peak_pool_size==10);
    CHECK(pocket_component_live_count(&components)==11);
    CHECK(pocket_virtual_collection_select(&collection,1006)==POCKET_MODEL_OK);
    CHECK(pocket_virtual_collection_focus(&collection,1007)==POCKET_MODEL_OK);

    CHECK(pocket_virtual_collection_set_window(&collection,50,8)==POCKET_MODEL_OK);
    CHECK(pocket_virtual_collection_stats(&collection,&stats)==POCKET_MODEL_OK);
    CHECK(stats.materialized_first==48&&stats.materialized_count==12&&stats.pool_size==12);
    CHECK(stats.selected_key==1006&&stats.focused_key==1007);
    CHECK(stats.recycle_count>0&&pocket_component_live_count(&components)==13);

    drinks.revision=1;
    uint64_t before=stats.bind_calls;
    CHECK(pocket_virtual_collection_refresh(&collection,1)==POCKET_MODEL_OK);
    CHECK(pocket_virtual_collection_stats(&collection,&stats)==POCKET_MODEL_OK);
    CHECK(stats.bind_calls==before+stats.materialized_count&&stats.revision==1);

    Fixture logs={500,5000,0,0};PocketComponentHandle grid=make_parent(&components,POCKET_COMPONENT_GRID);
    CHECK(pocket_component_handle_valid(grid));
    PocketVirtualCollection log_collection={0};
    PocketVirtualCollectionConfig gc={.components=&components,.parent=grid,.item_kind=POCKET_COMPONENT_TEXT,
        .overscan=4,.max_pool=24,.model={&logs,model_count,model_key,model_bind}};
    CHECK(pocket_virtual_collection_init(&log_collection,&gc)==POCKET_MODEL_OK);
    CHECK(pocket_virtual_collection_set_window(&log_collection,200,12)==POCKET_MODEL_OK);
    PocketVirtualCollectionStats log_stats;
    CHECK(pocket_virtual_collection_stats(&log_collection,&log_stats)==POCKET_MODEL_OK);
    CHECK(log_stats.model_count==500&&log_stats.pool_size<=20&&log_stats.materialized_count<=20);

    Fixture wifi={12,9000,0,0};PocketComponentHandle wifi_parent=make_parent(&components,POCKET_COMPONENT_VIEW);
    CHECK(pocket_component_handle_valid(wifi_parent));
    PocketRepeater repeater={0};PocketRepeaterConfig rc={.components=&components,.parent=wifi_parent,
        .item_kind=POCKET_COMPONENT_TEXT,.max_items=16,.model={&wifi,model_count,model_key,model_bind}};
    CHECK(pocket_repeater_init(&repeater,&rc)==POCKET_MODEL_OK);
    CHECK(pocket_repeater_sync(&repeater,1)==POCKET_MODEL_OK);
    CHECK(pocket_repeater_count(&repeater)==12&&pocket_repeater_bind_calls(&repeater)==12);
    wifi.count=8;
    CHECK(pocket_repeater_sync(&repeater,2)==POCKET_MODEL_OK&&pocket_repeater_count(&repeater)==8);

    struct rusage usage;CHECK(getrusage(RUSAGE_SELF,&usage)==0);
    printf("VIRTUAL_METRIC drinks=100 logs=500 wifi=8 peak_pool=%u log_pool=%u component_live=%zu pool_bytes=%zu rss_kib=%ld binds=%llu recycle=%llu\n",
           stats.peak_pool_size,log_stats.peak_pool_size,pocket_component_live_count(&components),
           stats.pool_bytes+log_stats.pool_bytes,usage.ru_maxrss,
           (unsigned long long)(stats.bind_calls+log_stats.bind_calls),
           (unsigned long long)(stats.recycle_count+log_stats.recycle_count));

    pocket_repeater_dispose(&repeater);
    pocket_virtual_collection_dispose(&log_collection);
    pocket_virtual_collection_dispose(&collection);
    CHECK(pocket_component_destroy(&components,wifi_parent)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_destroy(&components,grid)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_destroy(&components,list)==POCKET_COMPONENT_OK);
    CHECK(pocket_component_live_count(&components)==0);
    pocket_component_runtime_dispose(&components);pocket_layout_dispose(&layout);pocket_ui_tree_dispose(&tree);
    puts("VIRTUAL_LIST_OK");
    return 0;
}
