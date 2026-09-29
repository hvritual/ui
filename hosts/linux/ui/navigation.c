#include "navigation.h"

#include <stdlib.h>
#include <string.h>

#define NAV_DEFAULT_CAPACITY 32U
#define NAV_MAX_CAPACITY 64U
#define NAV_CANCELS_PER_PAGE 16U

typedef struct {
    PocketNavigationResourceKind kind;
    PocketNavigationCancelFn cancel;
    void *context;
    int live;
} CancelRecord;

typedef struct {
    int live;
    int cleaning;
    PocketNavigationPage page;
    CancelRecord cancels[NAV_CANCELS_PER_PAGE];
    uint32_t cancel_count;
} PageRecord;

typedef struct {
    PocketComponentRuntime *components;
    PageRecord *pages;
    uint32_t capacity;
    uint32_t count;
    PocketNavigationLifecycleFn lifecycle;
    void *lifecycle_context;
    PocketNavigationOwnerCleanupFn owner_cleanup;
    void *owner_cleanup_context;
} NavigationImpl;

static NavigationImpl *ni(PocketNavigationStack *stack){return stack?(NavigationImpl *)stack->impl:NULL;}
static const NavigationImpl *cni(const PocketNavigationStack *stack){return stack?(const NavigationImpl *)stack->impl:NULL;}
static void life(NavigationImpl *impl,uint64_t route,PocketNavigationLifecycle event,PocketNavigationTransition transition){
    if(impl->lifecycle)impl->lifecycle(impl->lifecycle_context,route,event,transition);
}
static int route_exists(const NavigationImpl *impl,uint64_t route){
    for(uint32_t i=0;i<impl->count;i++)if(impl->pages[i].live&&impl->pages[i].page.route_id==route)return 1;
    return 0;
}
static PocketNavigationStatus validate_page(NavigationImpl *impl,const PocketNavigationPage *page){
    PocketComponentSnapshot snapshot;
    if(!page||!page->route_id||!pocket_component_handle_valid(page->root)||page->owns_root>1U)
        return POCKET_NAV_INVALID_ARGUMENT;
    if(pocket_component_snapshot(impl->components,page->root,&snapshot)!=POCKET_COMPONENT_OK)
        return POCKET_NAV_STALE_COMPONENT;
    if(route_exists(impl,page->route_id))return POCKET_NAV_DUPLICATE_ROUTE;
    return POCKET_NAV_OK;
}
static void cancel_resources(PageRecord *record){
    record->cleaning=1;
    for(uint32_t i=record->cancel_count;i>0;i--){
        CancelRecord *c=&record->cancels[i-1U];
        if(c->live&&c->cancel){c->live=0;c->cancel(c->context,c->kind);}
    }
    record->cancel_count=0;
}
static PocketNavigationStatus cleanup_page(NavigationImpl *impl,PageRecord *record){
    if(!record||!record->live)return POCKET_NAV_EMPTY;
    uint64_t route=record->page.route_id;
    cancel_resources(record);
    if(impl->owner_cleanup)impl->owner_cleanup(impl->owner_cleanup_context,route);
    PocketNavigationStatus status=POCKET_NAV_OK;
    if(record->page.owns_root&&
       pocket_component_destroy(impl->components,record->page.root)!=POCKET_COMPONENT_OK)
        status=POCKET_NAV_STALE_COMPONENT;
    record->live=0;
    return status;
}
PocketNavigationStatus pocket_navigation_init(PocketNavigationStack *stack,const PocketNavigationConfig *config){
    if(!stack||stack->impl||!config||!config->components)return POCKET_NAV_INVALID_ARGUMENT;
    uint32_t capacity=config->capacity?config->capacity:NAV_DEFAULT_CAPACITY;
    if(!capacity||capacity>NAV_MAX_CAPACITY)return POCKET_NAV_INVALID_ARGUMENT;
    NavigationImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_NAV_FULL;
    impl->pages=calloc(capacity,sizeof(*impl->pages));
    if(!impl->pages){free(impl);return POCKET_NAV_FULL;}
    impl->components=config->components;
    impl->capacity=capacity;
    impl->lifecycle=config->lifecycle;
    impl->lifecycle_context=config->lifecycle_context;
    impl->owner_cleanup=config->owner_cleanup;
    impl->owner_cleanup_context=config->owner_cleanup_context;
    stack->impl=impl;
    return POCKET_NAV_OK;
}
void pocket_navigation_dispose(PocketNavigationStack *stack){
    NavigationImpl *impl=ni(stack);
    if(!impl)return;
    while(impl->count){
        PageRecord *r=&impl->pages[impl->count-1U];
        uint64_t route=r->page.route_id;
        (void)cleanup_page(impl,r);
        life(impl,route,POCKET_NAV_DESTROYED,POCKET_NAV_RESET);
        impl->count--;
    }
    free(impl->pages);free(impl);stack->impl=NULL;
}
PocketNavigationStatus pocket_navigation_push(PocketNavigationStack *stack,const PocketNavigationPage *page){
    NavigationImpl *impl=ni(stack);
    if(!impl)return POCKET_NAV_INVALID_ARGUMENT;
    PocketNavigationStatus valid=validate_page(impl,page);
    if(valid!=POCKET_NAV_OK)return valid;
    if(impl->count>=impl->capacity)return POCKET_NAV_FULL;
    if(impl->count)life(impl,impl->pages[impl->count-1U].page.route_id,POCKET_NAV_WILL_DISAPPEAR,POCKET_NAV_PUSH);
    life(impl,page->route_id,POCKET_NAV_WILL_APPEAR,POCKET_NAV_PUSH);
    PageRecord *r=&impl->pages[impl->count++];memset(r,0,sizeof(*r));r->live=1;r->page=*page;
    if(impl->count>1)life(impl,impl->pages[impl->count-2U].page.route_id,POCKET_NAV_DID_DISAPPEAR,POCKET_NAV_PUSH);
    life(impl,page->route_id,POCKET_NAV_DID_APPEAR,POCKET_NAV_PUSH);
    return POCKET_NAV_OK;
}
PocketNavigationStatus pocket_navigation_pop(PocketNavigationStack *stack){
    NavigationImpl *impl=ni(stack);
    if(!impl)return POCKET_NAV_INVALID_ARGUMENT;
    if(!impl->count)return POCKET_NAV_EMPTY;
    if(impl->count==1)return POCKET_NAV_ROOT;
    PageRecord *old=&impl->pages[impl->count-1U],*next=&impl->pages[impl->count-2U];
    uint64_t old_route=old->page.route_id,next_route=next->page.route_id;
    life(impl,old_route,POCKET_NAV_WILL_DISAPPEAR,POCKET_NAV_POP);life(impl,next_route,POCKET_NAV_WILL_APPEAR,POCKET_NAV_POP);
    PocketNavigationStatus status=cleanup_page(impl,old);
    impl->count--;
    life(impl,old_route,POCKET_NAV_DID_DISAPPEAR,POCKET_NAV_POP);
    life(impl,old_route,POCKET_NAV_DESTROYED,POCKET_NAV_POP);
    life(impl,next_route,POCKET_NAV_DID_APPEAR,POCKET_NAV_POP);
    return status;
}
PocketNavigationStatus pocket_navigation_replace(PocketNavigationStack *stack,const PocketNavigationPage *page){
    NavigationImpl *impl=ni(stack);
    if(!impl)return POCKET_NAV_INVALID_ARGUMENT;
    if(!impl->count)return pocket_navigation_push(stack,page);
    PocketNavigationStatus valid=validate_page(impl,page);
    if(valid!=POCKET_NAV_OK)return valid;
    PageRecord *old=&impl->pages[impl->count-1U];uint64_t old_route=old->page.route_id;
    life(impl,old_route,POCKET_NAV_WILL_DISAPPEAR,POCKET_NAV_REPLACE);life(impl,page->route_id,POCKET_NAV_WILL_APPEAR,POCKET_NAV_REPLACE);
    PocketNavigationStatus status=cleanup_page(impl,old);
    life(impl,old_route,POCKET_NAV_DID_DISAPPEAR,POCKET_NAV_REPLACE);
    life(impl,old_route,POCKET_NAV_DESTROYED,POCKET_NAV_REPLACE);
    memset(old,0,sizeof(*old));old->live=1;old->page=*page;
    life(impl,page->route_id,POCKET_NAV_DID_APPEAR,POCKET_NAV_REPLACE);
    return status;
}
PocketNavigationStatus pocket_navigation_reset(PocketNavigationStack *stack,const PocketNavigationPage *page){
    NavigationImpl *impl=ni(stack);
    if(!impl)return POCKET_NAV_INVALID_ARGUMENT;
    PocketNavigationStatus valid=validate_page(impl,page);
    if(valid!=POCKET_NAV_OK)return valid;
    while(impl->count){
        PageRecord *r=&impl->pages[impl->count-1U];
        uint64_t route=r->page.route_id;
        life(impl,route,POCKET_NAV_WILL_DISAPPEAR,POCKET_NAV_RESET);
        (void)cleanup_page(impl,r);
        life(impl,route,POCKET_NAV_DID_DISAPPEAR,POCKET_NAV_RESET);
        life(impl,route,POCKET_NAV_DESTROYED,POCKET_NAV_RESET);
        impl->count--;
    }
    return pocket_navigation_push(stack,page);
}
PocketNavigationStatus pocket_navigation_back(PocketNavigationStack *stack,int *consumed){
    if(!consumed)return POCKET_NAV_INVALID_ARGUMENT;
    *consumed=0;
    NavigationImpl *impl=ni(stack);
    if(!impl)return POCKET_NAV_INVALID_ARGUMENT;
    if(!impl->count)return POCKET_NAV_EMPTY;
    if(impl->count==1)return POCKET_NAV_ROOT;
    PocketNavigationStatus status=pocket_navigation_pop(stack);
    if(status==POCKET_NAV_OK)*consumed=1;
    return status;
}
PocketNavigationStatus pocket_navigation_top(const PocketNavigationStack *stack,PocketNavigationPage *out){
    const NavigationImpl *impl=cni(stack);
    if(!impl||!out)return POCKET_NAV_INVALID_ARGUMENT;
    if(!impl->count)return POCKET_NAV_EMPTY;
    *out=impl->pages[impl->count-1U].page;
    return POCKET_NAV_OK;
}
size_t pocket_navigation_count(const PocketNavigationStack *stack){
    const NavigationImpl *impl=cni(stack);
    return impl?impl->count:0;
}
PocketNavigationStatus pocket_navigation_register_cancel(PocketNavigationStack *stack,uint64_t route_id,
                                                          PocketNavigationResourceKind kind,
                                                          PocketNavigationCancelFn cancel,void *context){
    NavigationImpl *impl=ni(stack);
    if(!impl||!route_id||!cancel||kind<POCKET_NAV_RESOURCE_TIMER||kind>POCKET_NAV_RESOURCE_INPUT_CAPTURE)
        return POCKET_NAV_INVALID_ARGUMENT;
    for(uint32_t i=0;i<impl->count;i++){
        PageRecord *r=&impl->pages[i];
        if(r->live&&r->page.route_id==route_id){
            if(r->cleaning)return POCKET_NAV_RESOURCE_FULL;
            if(r->cancel_count>=NAV_CANCELS_PER_PAGE)return POCKET_NAV_RESOURCE_FULL;
            CancelRecord *c=&r->cancels[r->cancel_count++];c->kind=kind;c->cancel=cancel;c->context=context;c->live=1;
            return POCKET_NAV_OK;
        }
    }
    return POCKET_NAV_EMPTY;
}
