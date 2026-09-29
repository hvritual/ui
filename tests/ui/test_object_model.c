#include "hosts/linux/ui/object.h"

#include <stdio.h>
#include <string.h>

static int expect(int ok, const char *name) {
    if(ok) return 1;
    fprintf(stderr, "FAIL %s\n", name);
    return 0;
}
int main(void) {
    PocketUiTree tree = {0};
    PocketUiTreeConfig cfg = {0};
    cfg.initial_capacity = 4;
    cfg.update_queue_capacity = 16;
    cfg.update_budget = 8;
    if(!expect(pocket_ui_tree_init(&tree, &cfg) == POCKET_UI_OK, "init")) return 1;

    PocketUiHandle root = {0};
    if(!expect(pocket_ui_create(&tree, POCKET_UI_CONTAINER, (PocketUiHandle){0}, &root) == POCKET_UI_OK,
               "root-create")) return 1;
    if(!expect(pocket_ui_mount(&tree, root) == POCKET_UI_OK, "root-mount")) return 1;

    PocketUiNodeType types[] = {
        POCKET_UI_NODE, POCKET_UI_COMPONENT, POCKET_UI_CONTAINER, POCKET_UI_TEXT,
        POCKET_UI_IMAGE, POCKET_UI_INPUT, POCKET_UI_SCROLL, POCKET_UI_CUSTOM
    };
    PocketUiHandle handles[8];
    uint64_t last_id = 0;
    for(size_t i = 0; i < sizeof(types)/sizeof(types[0]); ++i) {
        if(!expect(pocket_ui_create(&tree, types[i], root, &handles[i]) == POCKET_UI_OK, "type-create")) return 1;
        if(!expect(pocket_ui_mount(&tree, handles[i]) == POCKET_UI_OK, "type-mount")) return 1;
        PocketUiSnapshot snap;
        if(!expect(pocket_ui_snapshot(&tree, handles[i], &snap) == POCKET_UI_OK, "type-snapshot")) return 1;
        if(!expect(snap.type == types[i] && snap.stable_id > last_id &&
                   snap.parent.slot == root.slot, "type-semantics")) return 1;
        last_id = snap.stable_id;
    }
    PocketUiSnapshot root_snap;
    if(!expect(pocket_ui_snapshot(&tree, root, &root_snap) == POCKET_UI_OK &&
               root_snap.child_count == 8U, "parent-child")) return 1;
    if(!expect(pocket_ui_live_count(&tree) == 9U, "live-count")) return 1;

    PocketUiProperties props;
    memset(&props, 0, sizeof(props));
    props.geometry = (PocketUiRect){10,20,30,40};
    props.visible = 1; props.enabled = 1; props.opacity_256 = 200;
    props.semantic_state = 7; props.style_ref = 9; props.focusable = 1; props.clickable = 1;
    PocketUiPropertyFields all = POCKET_UI_PROP_GEOMETRY | POCKET_UI_PROP_VISIBLE |
        POCKET_UI_PROP_ENABLED | POCKET_UI_PROP_OPACITY | POCKET_UI_PROP_SEMANTIC_STATE |
        POCKET_UI_PROP_STYLE_REF | POCKET_UI_PROP_FOCUSABLE | POCKET_UI_PROP_CLICKABLE |
        POCKET_UI_PROP_RESOURCE | POCKET_UI_PROP_TEXT;
    if(!expect(pocket_ui_update(&tree, handles[3], all, &props) == POCKET_UI_OK, "update")) return 1;
    if(!expect(pocket_ui_update(&tree, handles[3], (PocketUiPropertyFields)(1U<<31), &props) ==
               POCKET_UI_INVALID_ARGUMENT, "unknown-property-bit")) return 1;
    PocketUiSnapshot text;
    if(!expect(pocket_ui_snapshot(&tree, handles[3], &text) == POCKET_UI_OK &&
               text.properties.geometry.width == 30 && text.properties.opacity_256 == 200 &&
               text.properties.semantic_state == 7 && text.properties.style_ref == 9,
               "properties")) return 1;

    PocketUiHandle stale = handles[2];
    if(!expect(pocket_ui_destroy(&tree, stale) == POCKET_UI_OK, "destroy")) return 1;
    if(!expect(pocket_ui_snapshot(&tree, stale, &text) == POCKET_UI_STALE_HANDLE, "stale")) return 1;

    if(!expect(pocket_ui_destroy(&tree, root) == POCKET_UI_OK, "destroy-root")) return 1;
    if(!expect(pocket_ui_live_count(&tree) == 0U, "destroy-subtree")) return 1;

    uint32_t previous_generation = 0;
    uint64_t previous_id = last_id;
    for(int i = 0; i < 1200; ++i) {
        PocketUiHandle n;
        if(!expect(pocket_ui_create(&tree, POCKET_UI_COMPONENT, (PocketUiHandle){0}, &n) == POCKET_UI_OK,
                   "stress-create")) return 1;
        if(!expect(pocket_ui_mount(&tree, n) == POCKET_UI_OK, "stress-mount")) return 1;
        if(!expect(pocket_ui_layout(&tree, n) == POCKET_UI_OK, "stress-layout")) return 1;
        if(!expect(pocket_ui_paint(&tree, n) == POCKET_UI_OK, "stress-paint")) return 1;
        PocketUiSnapshot snap;
        if(!expect(pocket_ui_snapshot(&tree, n, &snap) == POCKET_UI_OK &&
                   snap.stable_id > previous_id, "stress-id")) return 1;
        if(i > 0 && n.slot == 1U && !expect(n.generation > previous_generation, "stress-generation")) return 1;
        previous_generation = n.generation;
        previous_id = snap.stable_id;
        if(!expect(pocket_ui_unmount(&tree, n) == POCKET_UI_OK, "stress-unmount")) return 1;
        if(!expect(pocket_ui_destroy(&tree, n) == POCKET_UI_OK, "stress-destroy")) return 1;
    }
    if(!expect(pocket_ui_live_count(&tree) == 0U, "stress-no-leak")) return 1;

    PocketUiHandle depth_root={0}, depth_parent={0};
    if(!expect(pocket_ui_create(&tree,POCKET_UI_CONTAINER,(PocketUiHandle){0},&depth_root)==POCKET_UI_OK &&
               pocket_ui_mount(&tree,depth_root)==POCKET_UI_OK,"depth-root")) return 1;
    depth_parent=depth_root;
    for(int depth=2; depth<=256; ++depth) {
        PocketUiHandle next={0};
        if(!expect(pocket_ui_create(&tree,POCKET_UI_CONTAINER,depth_parent,&next)==POCKET_UI_OK &&
                   pocket_ui_mount(&tree,next)==POCKET_UI_OK,"depth-chain")) return 1;
        depth_parent=next;
    }
    PocketUiHandle too_deep={0};
    if(!expect(pocket_ui_create(&tree,POCKET_UI_CONTAINER,depth_parent,&too_deep)==
               POCKET_UI_RESOURCE_EXHAUSTED,"depth-bound")) return 1;
    if(!expect(pocket_ui_destroy(&tree,depth_root)==POCKET_UI_OK &&
               pocket_ui_live_count(&tree)==0U,"depth-cleanup")) return 1;

    pocket_ui_tree_dispose(&tree);
    if(!expect(tree.impl == NULL, "dispose")) return 1;
    puts("UI_OBJECT_MODEL_OK cycles=1200");
    return 0;
}
