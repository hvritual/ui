#include "component.h"

#include <stdlib.h>
#include <string.h>

#define COMPONENT_DEFAULT_CAPACITY 512U
#define COMPONENT_MAX_CAPACITY 4096U
#define COMPONENT_STYLE_BASE 0x1000ULL

typedef struct {
    uint32_t generation;
    int live;
    PocketComponentKind kind;
    PocketComponentHandle parent;
    PocketComponentHandle first_child;
    PocketComponentHandle next_sibling;
    PocketComponentHandle prev_sibling;
    uint32_t child_count;
    PocketUiHandle root;
    PocketComponentProps props;
    PocketComponentEventFn handler;
    void *handler_context;
} ComponentRecord;

typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketStyleRuntime *styles;
    ComponentRecord *records;
    uint32_t capacity;
    size_t live_count;
} ComponentImpl;

static ComponentImpl *ci(PocketComponentRuntime *runtime) {
    return runtime ? (ComponentImpl *)runtime->impl : NULL;
}
static const ComponentImpl *cci(const PocketComponentRuntime *runtime) {
    return runtime ? (const ComponentImpl *)runtime->impl : NULL;
}
int pocket_component_handle_valid(PocketComponentHandle handle) {
    return handle.slot != 0U && handle.generation != 0U;
}
static int handle_equal(PocketComponentHandle a, PocketComponentHandle b) {
    return a.slot == b.slot && a.generation == b.generation;
}
static PocketComponentHandle zero_component(void) {
    PocketComponentHandle h = {0,0}; return h;
}
static ComponentRecord *find_component(ComponentImpl *impl, PocketComponentHandle handle) {
    if(!impl || !pocket_component_handle_valid(handle) || handle.slot > impl->capacity) return NULL;
    ComponentRecord *record=&impl->records[handle.slot-1U];
    return record->live && record->generation==handle.generation ? record : NULL;
}
static const ComponentRecord *find_component_const(const ComponentImpl *impl,
                                                    PocketComponentHandle handle) {
    if(!impl || !pocket_component_handle_valid(handle) || handle.slot > impl->capacity) return NULL;
    const ComponentRecord *record=&impl->records[handle.slot-1U];
    return record->live && record->generation==handle.generation ? record : NULL;
}
static PocketComponentHandle record_handle(const ComponentImpl *impl,
                                           const ComponentRecord *record) {
    PocketComponentHandle h={0,0};
    if(!impl || !record) return h;
    size_t index=(size_t)(record-impl->records);
    if(index>=impl->capacity) return h;
    h.slot=(uint32_t)index+1U; h.generation=record->generation; return h;
}
static int kind_valid(PocketComponentKind kind) {
    return kind>=POCKET_COMPONENT_VIEW && kind<POCKET_COMPONENT_KIND_COUNT;
}
static PocketUiNodeType ui_kind(PocketComponentKind kind) {
    switch(kind) {
        case POCKET_COMPONENT_TEXT: return POCKET_UI_TEXT;
        case POCKET_COMPONENT_IMAGE:
        case POCKET_COMPONENT_ICON: return POCKET_UI_IMAGE;
        case POCKET_COMPONENT_TEXT_FIELD: return POCKET_UI_INPUT;
        case POCKET_COMPONENT_SCROLL:
        case POCKET_COMPONENT_LIST:
        case POCKET_COMPONENT_GRID: return POCKET_UI_SCROLL;
        case POCKET_COMPONENT_VIEW:
        case POCKET_COMPONENT_MODAL:
        case POCKET_COMPONENT_DIALOG:
        case POCKET_COMPONENT_TOAST:
        case POCKET_COMPONENT_LOADING:
        case POCKET_COMPONENT_ERROR_STATE: return POCKET_UI_CONTAINER;
        default: return POCKET_UI_COMPONENT;
    }
}
static int clickable_kind(PocketComponentKind kind) {
    return kind==POCKET_COMPONENT_BUTTON || kind==POCKET_COMPONENT_ICON_BUTTON ||
           kind==POCKET_COMPONENT_TOGGLE || kind==POCKET_COMPONENT_CHECKBOX ||
           kind==POCKET_COMPONENT_RADIO || kind==POCKET_COMPONENT_SLIDER ||
           kind==POCKET_COMPONENT_TEXT_FIELD;
}
static int value_kind(PocketComponentKind kind) {
    return kind==POCKET_COMPONENT_SLIDER || kind==POCKET_COMPONENT_PROGRESS;
}
static int toggle_kind(PocketComponentKind kind) {
    return kind==POCKET_COMPONENT_TOGGLE || kind==POCKET_COMPONENT_CHECKBOX ||
           kind==POCKET_COMPONENT_RADIO;
}
uint64_t pocket_component_default_style_ref(PocketComponentKind kind) {
    return kind_valid(kind) ? COMPONENT_STYLE_BASE+(uint64_t)kind : 0;
}
PocketComponentProps pocket_component_props_default(PocketComponentKind kind) {
    PocketComponentProps p;
    memset(&p,0,sizeof(p));
    p.style_ref=pocket_component_default_style_ref(kind);
    p.visible=1;
    p.min_value=0; p.max_value=100;
    p.max_length=256;
    p.input_kind=POCKET_TEXT_INPUT_TEXT;
    return p;
}
static int props_valid(PocketComponentKind kind,const PocketComponentProps *p) {
    if(!p || p->visible>1U || p->disabled>1U || p->read_only>1U || p->secure>1U ||
       (p->states&~POCKET_STATE_ALL) || p->input_kind>POCKET_TEXT_INPUT_NUMBER ||
       p->min_value>p->max_value || p->value<p->min_value || p->value>p->max_value)
        return 0;
    if(kind!=POCKET_COMPONENT_TEXT_FIELD &&
       (p->text_session_id || p->read_only || p->secure || p->input_kind!=POCKET_TEXT_INPUT_TEXT))
        return 0;
    if(kind==POCKET_COMPONENT_TEXT_FIELD && p->secure && p->input_kind!=POCKET_TEXT_INPUT_PASSWORD)
        return 0;
    if(kind==POCKET_COMPONENT_RADIO && !p->group_id) return 0;
    return 1;
}
static PocketLayoutSpec default_layout(PocketComponentKind kind) {
    PocketLayoutSpec s=pocket_layout_spec_default();
    switch(kind) {
        case POCKET_COMPONENT_VIEW:
        case POCKET_COMPONENT_MODAL:
        case POCKET_COMPONENT_DIALOG:
        case POCKET_COMPONENT_TOAST:
        case POCKET_COMPONENT_LOADING:
        case POCKET_COMPONENT_ERROR_STATE:
            s.mode=POCKET_LAYOUT_STACK; break;
        case POCKET_COMPONENT_BUTTON:
        case POCKET_COMPONENT_ICON_BUTTON:
            s.mode=POCKET_LAYOUT_ROW; s.align_items=POCKET_ALIGN_CENTER; s.gap=8; break;
        case POCKET_COMPONENT_SCROLL:
        case POCKET_COMPONENT_LIST:
            s.mode=POCKET_LAYOUT_COLUMN; s.gap=8; break;
        case POCKET_COMPONENT_GRID:
            s.mode=POCKET_LAYOUT_GRID; s.grid_columns=2; s.gap=8; break;
        default: s.mode=POCKET_LAYOUT_LEAF; break;
    }
    return s;
}
static PocketComponentStatus sync_ui(ComponentImpl *impl,ComponentRecord *record,
                                     uint32_t extra_fields) {
    PocketUiSnapshot snap;
    if(pocket_ui_snapshot(impl->tree,record->root,&snap)!=POCKET_UI_OK)
        return POCKET_COMPONENT_UI_ERROR;
    PocketUiProperties p=snap.properties;
    p.visible=record->props.visible;
    p.enabled=!record->props.disabled;
    p.opacity_256=256;
    p.semantic_state=record->props.states |
        (record->props.disabled?POCKET_STATE_DISABLED:0U);
    p.style_ref=record->props.style_ref;
    p.focusable=(record->kind==POCKET_COMPONENT_TEXT_FIELD);
    p.clickable=clickable_kind(record->kind);
    uint32_t fields=POCKET_UI_PROP_VISIBLE|POCKET_UI_PROP_ENABLED|
                    POCKET_UI_PROP_SEMANTIC_STATE|POCKET_UI_PROP_STYLE_REF|
                    POCKET_UI_PROP_FOCUSABLE|POCKET_UI_PROP_CLICKABLE|extra_fields;
    if(record->props.resource_ref) fields|=POCKET_UI_PROP_RESOURCE;
    if(record->props.text_ref) fields|=POCKET_UI_PROP_TEXT;
    return pocket_ui_update(impl->tree,record->root,fields,&p)==POCKET_UI_OK ?
           POCKET_COMPONENT_OK : POCKET_COMPONENT_UI_ERROR;
}
static ComponentRecord *allocate_record(ComponentImpl *impl,PocketComponentHandle *out) {
    for(uint32_t i=0;i<impl->capacity;i++) {
        ComponentRecord *r=&impl->records[i];
        if(!r->live) {
            uint32_t generation=r->generation+1U;
            if(!generation) generation=1U;
            memset(r,0,sizeof(*r));
            r->generation=generation; r->live=1;
            out->slot=i+1U; out->generation=generation;
            return r;
        }
    }
    return NULL;
}
static void link_child(ComponentImpl *impl,ComponentRecord *parent,PocketComponentHandle child) {
    ComponentRecord *c=find_component(impl,child);
    if(!parent||!c)return;
    c->parent=record_handle(impl,parent);
    c->next_sibling=parent->first_child;
    if(pocket_component_handle_valid(parent->first_child)) {
        ComponentRecord *old=find_component(impl,parent->first_child);
        if(old) old->prev_sibling=child;
    }
    parent->first_child=child; parent->child_count++;
}
static void unlink_record(ComponentImpl *impl,ComponentRecord *record) {
    if(!pocket_component_handle_valid(record->parent)) return;
    ComponentRecord *parent=find_component(impl,record->parent);
    if(!parent)return;
    if(pocket_component_handle_valid(record->prev_sibling)) {
        ComponentRecord *prev=find_component(impl,record->prev_sibling);
        if(prev) prev->next_sibling=record->next_sibling;
    } else parent->first_child=record->next_sibling;
    if(pocket_component_handle_valid(record->next_sibling)) {
        ComponentRecord *next=find_component(impl,record->next_sibling);
        if(next) next->prev_sibling=record->prev_sibling;
    }
    if(parent->child_count) parent->child_count--;
}
static void invalidate_subtree(ComponentImpl *impl,ComponentRecord *record) {
    PocketComponentHandle child=record->first_child;
    while(pocket_component_handle_valid(child)) {
        ComponentRecord *c=find_component(impl,child);
        if(!c)break;
        PocketComponentHandle next=c->next_sibling;
        invalidate_subtree(impl,c);
        child=next;
    }
    record->live=0; record->first_child=zero_component(); record->child_count=0;
    record->handler=NULL; record->handler_context=NULL;
    record->root=(PocketUiHandle){0};
    if(impl->live_count) impl->live_count--;
}
PocketComponentStatus pocket_component_runtime_init(PocketComponentRuntime *runtime,
                                                     const PocketComponentRuntimeConfig *config) {
    if(!runtime || runtime->impl || !config || !config->tree || !config->layout)
        return POCKET_COMPONENT_INVALID_ARGUMENT;
    uint32_t capacity=config->capacity?config->capacity:COMPONENT_DEFAULT_CAPACITY;
    if(!capacity || capacity>COMPONENT_MAX_CAPACITY) return POCKET_COMPONENT_INVALID_ARGUMENT;
    ComponentImpl *impl=calloc(1,sizeof(*impl));
    if(!impl)return POCKET_COMPONENT_RESOURCE_EXHAUSTED;
    impl->records=calloc(capacity,sizeof(*impl->records));
    if(!impl->records){free(impl);return POCKET_COMPONENT_RESOURCE_EXHAUSTED;}
    impl->tree=config->tree;impl->layout=config->layout;impl->styles=config->styles;impl->capacity=capacity;
    runtime->impl=impl;return POCKET_COMPONENT_OK;
}
void pocket_component_runtime_dispose(PocketComponentRuntime *runtime) {
    ComponentImpl *impl=ci(runtime);if(!impl)return;
    for(uint32_t i=0;i<impl->capacity;i++) if(impl->records[i].live &&
        !pocket_component_handle_valid(impl->records[i].parent)) {
        (void)pocket_ui_destroy(impl->tree,impl->records[i].root);
        invalidate_subtree(impl,&impl->records[i]);
    }
    free(impl->records);free(impl);runtime->impl=NULL;
}
PocketComponentStatus pocket_component_create(PocketComponentRuntime *runtime,
                                               PocketComponentKind kind,
                                               PocketComponentHandle parent,
                                               const PocketComponentProps *props,
                                               PocketComponentHandle *out) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *parent_record=NULL;PocketComponentProps actual;
    if(!impl||!out||!kind_valid(kind))return POCKET_COMPONENT_INVALID_ARGUMENT;
    if(pocket_component_handle_valid(parent)) {
        parent_record=find_component(impl,parent);
        if(!parent_record)return POCKET_COMPONENT_STALE_HANDLE;
    }
    actual=props?*props:pocket_component_props_default(kind);
    if(!actual.style_ref)actual.style_ref=pocket_component_default_style_ref(kind);
    if(!props)actual=pocket_component_props_default(kind);
    if(!props_valid(kind,&actual))return POCKET_COMPONENT_INVALID_ARGUMENT;
    PocketComponentHandle handle={0};ComponentRecord *record=allocate_record(impl,&handle);
    if(!record)return POCKET_COMPONENT_RESOURCE_EXHAUSTED;
    record->kind=kind;record->props=actual;
    PocketUiHandle ui_parent=parent_record?parent_record->root:(PocketUiHandle){0};
    if(pocket_ui_create(impl->tree,ui_kind(kind),ui_parent,&record->root)!=POCKET_UI_OK) {
        record->live=0;return POCKET_COMPONENT_UI_ERROR;
    }
    if(pocket_ui_mount(impl->tree,record->root)!=POCKET_UI_OK) {
        (void)pocket_ui_destroy(impl->tree,record->root);record->live=0;return POCKET_COMPONENT_UI_ERROR;
    }
    PocketLayoutSpec layout=default_layout(kind);
    if(pocket_layout_set(impl->layout,record->root,&layout)!=POCKET_UI_OK) {
        (void)pocket_ui_destroy(impl->tree,record->root);record->live=0;return POCKET_COMPONENT_UI_ERROR;
    }
    if(sync_ui(impl,record,0)!=POCKET_COMPONENT_OK) {
        (void)pocket_ui_destroy(impl->tree,record->root);record->live=0;return POCKET_COMPONENT_UI_ERROR;
    }
    if(parent_record)link_child(impl,parent_record,handle);
    impl->live_count++;*out=handle;return POCKET_COMPONENT_OK;
}
PocketComponentStatus pocket_component_destroy(PocketComponentRuntime *runtime,
                                                PocketComponentHandle component) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    PocketUiStatus ui_status=pocket_ui_destroy(impl->tree,record->root);
    unlink_record(impl,record);
    invalidate_subtree(impl,record);
    return ui_status==POCKET_UI_OK?POCKET_COMPONENT_OK:POCKET_COMPONENT_UI_ERROR;
}
PocketComponentStatus pocket_component_snapshot(const PocketComponentRuntime *runtime,
                                                 PocketComponentHandle component,
                                                 PocketComponentSnapshot *out) {
    const ComponentImpl *impl=cci(runtime);const ComponentRecord *record=find_component_const(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(!out)return POCKET_COMPONENT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));out->kind=record->kind;out->handle=component;out->parent=record->parent;
    out->root=record->root;out->props=record->props;out->child_count=record->child_count;
    return POCKET_COMPONENT_OK;
}
size_t pocket_component_live_count(const PocketComponentRuntime *runtime) {
    const ComponentImpl *impl=cci(runtime);return impl?impl->live_count:0;
}
PocketComponentStatus pocket_component_set_layout(PocketComponentRuntime *runtime,
                                                   PocketComponentHandle component,
                                                   const PocketLayoutSpec *spec) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(!spec)return POCKET_COMPONENT_INVALID_ARGUMENT;
    return pocket_layout_set(impl->layout,record->root,spec)==POCKET_UI_OK ?
           POCKET_COMPONENT_OK:POCKET_COMPONENT_UI_ERROR;
}
PocketComponentStatus pocket_component_set_event_handler(PocketComponentRuntime *runtime,
                                                          PocketComponentHandle component,
                                                          PocketComponentEventFn handler,
                                                          void *context) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    record->handler=handler;record->handler_context=context;return POCKET_COMPONENT_OK;
}
static void emit(ComponentImpl *impl,ComponentRecord *record,PocketComponentEventType type) {
    if(!record->handler)return;
    PocketComponentEvent event={type,record->props.value,record->props.states|
        (record->props.disabled?POCKET_STATE_DISABLED:0U)};
    record->handler(record->handler_context,record_handle(impl,record),&event);
}
PocketComponentStatus pocket_component_set_states(PocketComponentRuntime *runtime,
                                                   PocketComponentHandle component,
                                                   uint32_t states) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(states&~POCKET_STATE_ALL)return POCKET_COMPONENT_INVALID_ARGUMENT;
    record->props.states=states&~POCKET_STATE_DISABLED;
    record->props.disabled=(states&POCKET_STATE_DISABLED)?1U:record->props.disabled;
    return sync_ui(impl,record,0);
}
static PocketComponentStatus set_checked(ComponentImpl *impl,ComponentRecord *record,int checked) {
    if(checked)record->props.states|=POCKET_STATE_CHECKED;
    else record->props.states&=~POCKET_STATE_CHECKED;
    PocketComponentStatus status=sync_ui(impl,record,0);
    if(status==POCKET_COMPONENT_OK)emit(impl,record,POCKET_COMPONENT_EVENT_TOGGLE_CHANGED);
    return status;
}
PocketComponentStatus pocket_component_set_value(PocketComponentRuntime *runtime,
                                                  PocketComponentHandle component,
                                                  int32_t value) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(!value_kind(record->kind))return POCKET_COMPONENT_UNSUPPORTED;
    if(value<record->props.min_value||value>record->props.max_value)return POCKET_COMPONENT_INVALID_ARGUMENT;
    record->props.value=value;
    if(record->kind==POCKET_COMPONENT_SLIDER)emit(impl,record,POCKET_COMPONENT_EVENT_VALUE_CHANGED);
    return POCKET_COMPONENT_OK;
}
PocketComponentStatus pocket_component_activate(PocketComponentRuntime *runtime,
                                                 PocketComponentHandle component) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(record->props.disabled)return POCKET_COMPONENT_DISABLED;
    if(!clickable_kind(record->kind))return POCKET_COMPONENT_UNSUPPORTED;
    if(record->kind==POCKET_COMPONENT_TOGGLE||record->kind==POCKET_COMPONENT_CHECKBOX)
        return set_checked(impl,record,(record->props.states&POCKET_STATE_CHECKED)==0);
    if(record->kind==POCKET_COMPONENT_RADIO) {
        if(!(record->props.states&POCKET_STATE_CHECKED)) {
            for(uint32_t i=0;i<impl->capacity;i++) {
                ComponentRecord *peer=&impl->records[i];
                if(!peer->live||peer==record||peer->kind!=POCKET_COMPONENT_RADIO||
                   peer->props.group_id!=record->props.group_id||
                   !handle_equal(peer->parent,record->parent))continue;
                if(peer->props.states&POCKET_STATE_CHECKED) {
                    peer->props.states&=~POCKET_STATE_CHECKED;
                    if(sync_ui(impl,peer,0)!=POCKET_COMPONENT_OK)return POCKET_COMPONENT_UI_ERROR;
                }
            }
            return set_checked(impl,record,1);
        }
        return POCKET_COMPONENT_OK;
    }
    if(record->kind==POCKET_COMPONENT_TEXT_FIELD) {
        emit(impl,record,POCKET_COMPONENT_EVENT_FOCUS_REQUEST);return POCKET_COMPONENT_OK;
    }
    emit(impl,record,POCKET_COMPONENT_EVENT_ACTIVATE);return POCKET_COMPONENT_OK;
}
PocketComponentStatus pocket_component_submit(PocketComponentRuntime *runtime,
                                               PocketComponentHandle component) {
    ComponentImpl *impl=ci(runtime);ComponentRecord *record=find_component(impl,component);
    if(!record)return POCKET_COMPONENT_STALE_HANDLE;
    if(record->kind!=POCKET_COMPONENT_TEXT_FIELD)return POCKET_COMPONENT_UNSUPPORTED;
    if(record->props.disabled||record->props.read_only)return POCKET_COMPONENT_DISABLED;
    emit(impl,record,POCKET_COMPONENT_EVENT_SUBMIT);return POCKET_COMPONENT_OK;
}
