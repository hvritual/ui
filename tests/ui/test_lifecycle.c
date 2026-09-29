#include "hosts/linux/ui/object.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int id;
    PocketUiEventAction action;
    int *trace;
    int *count;
} HandlerContext;

typedef struct {
    PocketUiTree *tree;
    PocketUiHandle parent;
    PocketUiHandle created;
    int *trace;
    int *count;
} GrowthHandlerContext;

static PocketUiEventAction growth_handler(void *context, PocketUiEvent *event) {
    GrowthHandlerContext *g = context;
    g->trace[(*g->count)++] = 10 + (int)event->phase;
    if(!pocket_ui_handle_valid(g->created)) {
        if(pocket_ui_create(g->tree, POCKET_UI_CUSTOM, g->parent, &g->created) != POCKET_UI_OK)
            return (PocketUiEventAction)99;
    }
    return POCKET_UI_EVENT_CONTINUE;
}

static PocketUiEventAction handler(void *context, PocketUiEvent *event) {
    HandlerContext *h = context;
    h->trace[(*h->count)++] = h->id * 10 + (int)event->phase;
    return h->action;
}
static int expect(int ok, const char *name) {
    if(ok) return 1;
    fprintf(stderr, "FAIL %s\n", name);
    return 0;
}
int main(void) {
    PocketUiTree tree = {0};
    PocketUiTreeConfig cfg = {0};
    cfg.initial_capacity = 8; cfg.update_queue_capacity = 5; cfg.update_budget = 2;
    if(!expect(pocket_ui_tree_init(&tree, &cfg) == POCKET_UI_OK, "init")) return 1;

    PocketUiHandle root, parent, child;
    if(!expect(pocket_ui_create(&tree, POCKET_UI_CONTAINER, (PocketUiHandle){0}, &root) == POCKET_UI_OK,
               "root-create")) return 1;
    if(!expect(pocket_ui_create(&tree, POCKET_UI_CONTAINER, root, &parent) == POCKET_UI_OK,
               "parent-create")) return 1;
    if(!expect(pocket_ui_create(&tree, POCKET_UI_INPUT, parent, &child) == POCKET_UI_OK,
               "child-create")) return 1;
    if(!expect(pocket_ui_mount(&tree, child) == POCKET_UI_LIFECYCLE_ERROR,
               "child-before-parent")) return 1;
    if(!expect(pocket_ui_mount(&tree, root) == POCKET_UI_OK &&
               pocket_ui_mount(&tree, parent) == POCKET_UI_OK &&
               pocket_ui_mount(&tree, child) == POCKET_UI_OK, "mount-order")) return 1;

    PocketUiProperties p;
    memset(&p, 0, sizeof(p));
    p.geometry=(PocketUiRect){1,2,3,4}; p.visible=1; p.enabled=1; p.opacity_256=256;
    if(!expect(pocket_ui_update(&tree, child, POCKET_UI_PROP_GEOMETRY, &p) == POCKET_UI_OK,
               "update")) return 1;
    if(!expect(pocket_ui_paint(&tree, child) == POCKET_UI_LIFECYCLE_ERROR,
               "paint-needs-layout")) return 1;
    if(!expect(pocket_ui_layout(&tree, child) == POCKET_UI_OK &&
               pocket_ui_paint(&tree, child) == POCKET_UI_OK, "layout-paint")) return 1;

    int trace[16] = {0}, count = 0;
    HandlerContext hr={1,POCKET_UI_EVENT_CONTINUE,trace,&count};
    HandlerContext hp={2,POCKET_UI_EVENT_CONTINUE,trace,&count};
    HandlerContext hc={3,POCKET_UI_EVENT_CONTINUE,trace,&count};
    pocket_ui_set_event_handler(&tree, root, handler, &hr);
    pocket_ui_set_event_handler(&tree, parent, handler, &hp);
    pocket_ui_set_event_handler(&tree, child, handler, &hc);
    PocketUiEvent event = {.type=7,.x=4,.y=5,.data=9};
    if(!expect(pocket_ui_dispatch_event(&tree, child, &event) == POCKET_UI_OK, "dispatch")) return 1;
    int expected[]={11,21,32,23,13};
    if(!expect(count==5, "event-count")) return 1;
    for(int i=0;i<5;i++) if(!expect(trace[i]==expected[i],"event-order")) return 1;

    /* Force an arena growth from inside capture. Dispatch must re-resolve all
     * node handles and continue without using stale arena pointers. */
    GrowthHandlerContext growth={&tree,root,{0},trace,&count};
    count=0;
    if(!expect(pocket_ui_set_event_handler(&tree,root,growth_handler,&growth)==POCKET_UI_OK,
               "growth-handler-set")) return 1;
    if(!expect(pocket_ui_dispatch_event(&tree,child,&event)==POCKET_UI_OK &&
               pocket_ui_handle_valid(growth.created) && count==5, "event-arena-growth")) return 1;
    if(!expect(pocket_ui_destroy(&tree,growth.created)==POCKET_UI_OK,"growth-cleanup")) return 1;
    if(!expect(pocket_ui_set_event_handler(&tree,root,handler,&hr)==POCKET_UI_OK,
               "growth-handler-restore")) return 1;

    count=0; hp.action=POCKET_UI_EVENT_CONSUME;
    if(!expect(pocket_ui_dispatch_event(&tree, child, &event) == POCKET_UI_OK &&
               event.consumed && !event.cancelled && count==2, "event-consume")) return 1;
    hp.action=POCKET_UI_EVENT_CONTINUE; hc.action=POCKET_UI_EVENT_CANCEL; count=0;
    if(!expect(pocket_ui_dispatch_event(&tree, child, &event) == POCKET_UI_OK &&
               event.cancelled && event.default_prevented && count==3, "event-cancel")) return 1;

    hc.action=POCKET_UI_EVENT_CONTINUE;
    for(int i=0;i<5;i++) {
        p.geometry.x=i;
        if(!expect(pocket_ui_enqueue_update(&tree, child, POCKET_UI_PROP_GEOMETRY, &p)==POCKET_UI_OK,
                   "queue-fill")) return 1;
    }
    if(!expect(pocket_ui_enqueue_update(&tree, child, POCKET_UI_PROP_GEOMETRY, &p)==POCKET_UI_QUEUE_FULL,
               "queue-full")) return 1;
    uint32_t applied=0;
    if(!expect(pocket_ui_drain_updates(&tree, 0, &applied)==POCKET_UI_BUDGET_EXHAUSTED &&
               applied==2 && pocket_ui_queued_updates(&tree)==3, "queue-budget")) return 1;
    if(!expect(pocket_ui_drain_updates(&tree, 3, &applied)==POCKET_UI_OK &&
               applied==3 && pocket_ui_queued_updates(&tree)==0, "queue-drain")) return 1;

    if(!expect(pocket_ui_enqueue_update(&tree, child, POCKET_UI_PROP_GEOMETRY, &p)==POCKET_UI_OK,
               "queue-stale-setup")) return 1;
    if(!expect(pocket_ui_destroy(&tree, child)==POCKET_UI_OK, "destroy-child")) return 1;
    if(!expect(pocket_ui_drain_updates(&tree, 8, &applied)==POCKET_UI_STALE_HANDLE &&
               pocket_ui_queued_updates(&tree)==0, "queue-stale")) return 1;

    if(!expect(pocket_ui_unmount(&tree, root)==POCKET_UI_OK, "unmount-subtree")) return 1;
    PocketUiSnapshot snap;
    if(!expect(pocket_ui_snapshot(&tree,parent,&snap)==POCKET_UI_OK &&
               snap.phase==POCKET_UI_PHASE_UNMOUNTED, "child-unmounted")) return 1;
    if(!expect(pocket_ui_destroy(&tree,root)==POCKET_UI_OK &&
               pocket_ui_live_count(&tree)==0, "destroy-all")) return 1;
    pocket_ui_tree_dispose(&tree);
    puts("UI_LIFECYCLE_OK");
    return 0;
}
