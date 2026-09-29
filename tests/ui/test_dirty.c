#include "hosts/linux/ui/object.h"
#include "hosts/linux/engine/fake.h"

#include <stdio.h>
#include <string.h>

static int expect(int ok, const char *name) {
    if(ok) return 1;
    fprintf(stderr, "FAIL %s\n", name);
    return 0;
}
static unsigned live_engine_nodes(PocketFakeEngine *engine) {
    unsigned count=0;
    for(unsigned i=0;i<POCKET_FAKE_ENGINE_MAX_NODES;i++) if(engine->nodes[i].live) count++;
    return count;
}
int main(void) {
    PocketFakeEngine engine={0};
    PocketEngineOpenConfig open={100,100,1};
    if(!expect(pocket_fake_engine_api.open(&engine,&open)==POCKET_ENGINE_OK,"engine-open")) return 1;

    PocketUiTree tree={0};
    PocketUiTreeConfig cfg={0};
    cfg.engine_api=&pocket_fake_engine_api; cfg.engine_context=&engine;
    cfg.initial_capacity=8; cfg.update_queue_capacity=8; cfg.update_budget=4;
    if(!expect(pocket_ui_tree_init(&tree,&cfg)==POCKET_UI_OK,"tree-init")) return 1;

    PocketUiHandle root, text;
    if(!expect(pocket_ui_create(&tree,POCKET_UI_CONTAINER,(PocketUiHandle){0},&root)==POCKET_UI_OK &&
               pocket_ui_create(&tree,POCKET_UI_TEXT,root,&text)==POCKET_UI_OK,
               "create")) return 1;
    if(!expect(live_engine_nodes(&engine)==2,"engine-node-create")) return 1;
    if(!expect(pocket_ui_mount(&tree,root)==POCKET_UI_OK &&
               pocket_ui_mount(&tree,text)==POCKET_UI_OK,"mount")) return 1;

    PocketUiDirtyFlags dirty=0;
    pocket_ui_take_dirty(&tree,root,&dirty);
    pocket_ui_take_dirty(&tree,text,&dirty);

    PocketUiProperties p;
    memset(&p,0,sizeof(p));
    p.geometry=(PocketUiRect){5,6,40,20};
    p.visible=1; p.enabled=1; p.opacity_256=128;
    if(!expect(pocket_ui_update(&tree,text,
        POCKET_UI_PROP_GEOMETRY|POCKET_UI_PROP_OPACITY|POCKET_UI_PROP_TEXT,&p)==POCKET_UI_OK,
        "update")) return 1;

    PocketUiSnapshot child,parent;
    if(!expect(pocket_ui_snapshot(&tree,text,&child)==POCKET_UI_OK &&
               (child.dirty & POCKET_UI_DIRTY_TEXT) &&
               (child.dirty & POCKET_UI_DIRTY_LAYOUT) &&
               (child.dirty & POCKET_UI_DIRTY_PAINT) &&
               (child.dirty & POCKET_UI_DIRTY_STYLE),"child-dirty")) return 1;
    if(!expect(pocket_ui_snapshot(&tree,root,&parent)==POCKET_UI_OK &&
               (parent.dirty & POCKET_UI_DIRTY_LAYOUT) &&
               (parent.dirty & POCKET_UI_DIRTY_PAINT),"parent-dirty")) return 1;

    if(!expect(engine.nodes[text.slot-1U].update.bounds.x==5 &&
               engine.nodes[text.slot-1U].update.opacity_256==128,
               "engine-property-bridge")) return 1;

    if(!expect(pocket_ui_layout(&tree,text)==POCKET_UI_OK &&
               pocket_ui_paint(&tree,text)==POCKET_UI_OK,"clear-work")) return 1;
    if(!expect(pocket_ui_snapshot(&tree,text,&child)==POCKET_UI_OK &&
               !(child.dirty & (POCKET_UI_DIRTY_LAYOUT|POCKET_UI_DIRTY_PAINT|
                                POCKET_UI_DIRTY_STYLE|POCKET_UI_DIRTY_TEXT)),
               "dirty-cleared")) return 1;

    PocketUiHandle stale=text;
    if(!expect(pocket_ui_destroy(&tree,root)==POCKET_UI_OK,"destroy")) return 1;
    if(!expect(live_engine_nodes(&engine)==0,"engine-node-remove")) return 1;
    if(!expect(pocket_ui_snapshot(&tree,stale,&child)==POCKET_UI_STALE_HANDLE,
               "native-stale-after-engine-remove")) return 1;
    if(!expect(pocket_ui_live_count(&tree)==0,"live-zero")) return 1;

    pocket_ui_tree_dispose(&tree);
    if(!expect(pocket_fake_engine_api.close(&engine)==POCKET_ENGINE_OK,"engine-close")) return 1;
    puts("UI_DIRTY_OK");
    return 0;
}
