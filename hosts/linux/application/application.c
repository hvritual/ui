#include "application.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BINDINGS 128U
#define QUEUED_EVENTS 128U
#define LISTS 4U
#define OVERLAYS 8U
#define SAFE_INTEGER UINT64_C(9007199254740991)

typedef struct Application Application;
typedef struct {
    Application *owner;
    PocketUiHandle target;
    uint64_t token, revision;
    unsigned mode;
} Binding;
typedef struct { PocketUiEvent event; PocketUiHandle target; uint64_t token, revision; } QueuedEvent;
typedef struct {
    PocketVirtualCollection collection;
    PocketComponentHandle parent;
    uint64_t id;
    uint32_t count;
} Collection;
typedef struct { uint64_t id; PocketComponentHandle root; int keyboard; } Overlay;
struct Application {
    PocketProgram program;
    PocketApplicationPolicy policy;
    PocketUiTree tree;
    PocketLayoutContext layout;
    PocketStyleRuntime styles;
    PocketComponentRuntime components;
    PocketNavigationStack navigation;
    PocketOverlayManager overlays;
    PocketInteractionRuntime interaction;
    PocketReactiveRuntime reactive;
    PocketKeyboard keyboard;
    PocketKeyboardConfig keyboard_config;
    char initial[POCKET_KEYBOARD_MAX_FIELDS][POCKET_KEYBOARD_MAX_CHARS + 1];
    unsigned configured_fields, keyboard_staged;
    Binding bindings[BINDINGS];
    QueuedEvent queued[QUEUED_EVENTS];
    unsigned queue_count, phase, disposing, dirty;
    Collection lists[LISTS];
    Overlay layers[OVERLAYS];
    uint64_t next_list, next_binding, now;
    unsigned height, media_ready;
    uint32_t background;
    PocketApplicationStats stats;
    char reply[POCKET_PROGRAM_TEXT_LIMIT + 1];
    const char *error;
};
static int fail(Application *a, const char *code) {
    if (a && !a->error) a->error = code;
    return 0;
}
static void erase(void *p, size_t n) {
    volatile unsigned char *b = p;
    while (n--) *b++ = 0;
}
static int same(PocketComponentHandle a, PocketComponentHandle b) {
    return a.slot == b.slot && a.generation == b.generation;
}
static uint64_t encoded(uint32_t slot, uint32_t generation) {
    uint64_t n = ((uint64_t)generation << 32) | slot;
    return n <= SAFE_INTEGER ? n : 0;
}
static int numbers(size_t count, const PocketProgramValue *args, int64_t *values) {
    for (size_t i = 0; i < count; ++i) {
        if (args[i].kind != POCKET_PROGRAM_INTEGER && args[i].kind != POCKET_PROGRAM_BOOLEAN) return 0;
        values[i] = args[i].integer;
    }
    return 1;
}
static int between(int64_t value, int64_t lo, int64_t hi) { return value >= lo && value <= hi; }
static int component(Application *a, int64_t token, PocketComponentHandle *out) {
    if (token <= 0) return 0;
    *out = (PocketComponentHandle){(uint32_t)token, (uint32_t)((uint64_t)token >> 32)};
    PocketComponentSnapshot s;
    return pocket_component_snapshot(&a->components, *out, &s) == POCKET_COMPONENT_OK;
}
static PocketUiHandle root(Application *a, PocketComponentHandle c) {
    PocketComponentSnapshot s;
    if (pocket_component_snapshot(&a->components, c, &s) != POCKET_COMPONENT_OK) return (PocketUiHandle){0};
    return s.root;
}
static PocketReactiveHandle signal_handle(int64_t token) {
    return (PocketReactiveHandle){(uint32_t)token, (uint32_t)((uint64_t)token >> 32)};
}
static int invoke(Application *a, const char *method, const char *input, char *result, size_t capacity, size_t *length) {
    if (a->error) return 0;
    if (!pocket_program_call(&a->program, method, input, strlen(input), result, capacity, length)) {
        PocketProgramSnapshot s;
        return fail(a, pocket_program_snapshot(&a->program, &s) && s.error ? s.error : "APPLICATION_CALL_FAILED");
    }
    return 1;
}
static PocketUiEventAction deliver(Application *a, uint64_t token, const PocketUiEvent *e) {
    char input[384], output[8];
    size_t n = 0;
    if (e->timestamp_ms > SAFE_INTEGER) { fail(a, "APPLICATION_EVENT_RANGE"); return POCKET_UI_EVENT_CANCEL; }
    int written = snprintf(input, sizeof(input),
        "{\"token\":%" PRIu64 ",\"type\":%u,\"phase\":%u,\"pointer\":%u,\"x\":%d,\"y\":%d,\"time\":%" PRIu64 "}",
        token, e->type, (unsigned)e->phase, e->pointer_id, e->x, e->y, e->timestamp_ms);
    if (written < 0 || (size_t)written >= sizeof(input)) { fail(a, "APPLICATION_EVENT_RANGE"); return POCKET_UI_EVENT_CANCEL; }
    unsigned previous = a->phase;
    a->phase = 2;
    int ok = invoke(a, "event", input, output, sizeof(output), &n);
    a->phase = previous;
    if (!ok || n != 1 || output[0] < '0' || output[0] > '2') {
        fail(a, "APPLICATION_EVENT_RESULT"); return POCKET_UI_EVENT_CANCEL;
    }
    return (PocketUiEventAction)(output[0] - '0');
}
static PocketUiEventAction on_event(void *context, PocketUiEvent *event) {
    Binding *b = context;
    Application *a = b->owner;
    if (a->disposing) return POCKET_UI_EVENT_CONTINUE;
    if (a->error) return POCKET_UI_EVENT_CANCEL;
    if (b->mode == 1 && (event->type != POCKET_UI_EVENT_TAP || event->phase != POCKET_UI_EVENT_TARGET)) return POCKET_UI_EVENT_CONTINUE;
    PocketProgramSnapshot s;
    if (!pocket_program_snapshot(&a->program, &s)) { fail(a, "APPLICATION_EVENT_OWNER"); return POCKET_UI_EVENT_CANCEL; }
    if (s.in_call) {
        /* Native lifecycle commands may cancel input. Never reenter QuickJS. */
        if (a->queue_count == QUEUED_EVENTS) { fail(a, "APPLICATION_EVENT_QUEUE_FULL"); return POCKET_UI_EVENT_CANCEL; }
        a->queued[a->queue_count++] = (QueuedEvent){*event, b->target, b->token, b->revision};
        return b->mode == 1 ? POCKET_UI_EVENT_CONSUME : POCKET_UI_EVENT_CONTINUE;
    }
    return deliver(a, b->token, event);
}
static int drain_events(Application *a) {
    unsigned count = a->queue_count;
    a->queue_count = 0;
    for (unsigned i = 0; i < count; ++i) {
        QueuedEvent e = a->queued[i];
        PocketUiSnapshot snapshot;
        if (pocket_ui_snapshot(&a->tree, e.target, &snapshot) != POCKET_UI_OK) continue;
        int bound = 0;
        for (unsigned j = 0; j < BINDINGS; ++j) {
            Binding *b = &a->bindings[j];
            if (b->owner && b->target.slot == e.target.slot && b->target.generation == e.target.generation && b->token == e.token && b->revision == e.revision) { bound = 1; break; }
        }
        if (bound) (void)deliver(a, e.token, &e.event);
        if (a->error) return 0;
    }
    return 1;
}
static int set_layout(Application *a, PocketComponentHandle c, int x, int y, int w, int h) {
    PocketLayoutSpec s = pocket_layout_spec_default();
    s.mode = POCKET_LAYOUT_ABSOLUTE;
    s.width = (PocketLength){POCKET_LENGTH_PX, w};
    s.height = (PocketLength){POCKET_LENGTH_PX, h};
    s.offset_x = (PocketLength){POCKET_LENGTH_PX, x};
    s.offset_y = (PocketLength){POCKET_LENGTH_PX, y};
    s.overflow = POCKET_OVERFLOW_CLIP;
    a->dirty = 1;
    return pocket_component_set_layout(&a->components, c, &s) == POCKET_COMPONENT_OK;
}
static void prune_layers(Application *a) {
    for (unsigned i = 0; i < OVERLAYS; ++i) if (a->layers[i].id) {
        PocketOverlaySnapshot s;
        if (pocket_overlay_snapshot(&a->overlays, a->layers[i].id, &s) != POCKET_OVERLAY_OK || !same(s.spec.root, a->layers[i].root)) a->layers[i] = (Overlay){0};
    }
}
static int remember_layer(Application *a, uint64_t id, int keyboard) {
    PocketOverlaySnapshot s;
    prune_layers(a);
    if (pocket_overlay_snapshot(&a->overlays, id, &s) != POCKET_OVERLAY_OK) return 0;
    for (unsigned i = 0; i < OVERLAYS; ++i) if (!a->layers[i].id) {
        a->layers[i] = (Overlay){id, s.spec.root, keyboard}; return 1;
    }
    return 0;
}
static int sync_layout(Application *a) {
    PocketNavigationPage page;
    if (pocket_navigation_top(&a->navigation, &page) != POCKET_NAV_OK) return 0;
    if (!a->dirty) return 1;
    PocketUiHandle page_root = root(a, page.root);
    if (pocket_layout_run(&a->layout, page_root, 1024, a->height) != POCKET_UI_OK) return 0;
    ++a->stats.layout_runs;
    a->dirty = 0;
    prune_layers(a);
    for (unsigned i = 0; i < OVERLAYS; ++i) if (a->layers[i].id && !a->layers[i].keyboard)
        if (pocket_layout_run(&a->layout, root(a, a->layers[i].root), 1024, a->height) != POCKET_UI_OK) return 0;
    return pocket_interaction_set_scene_root(&a->interaction, page_root) == POCKET_INTERACTION_OK;
}
static uint32_t model_count(void *context) { return ((Collection *)context)->count; }
static int model_key(void *context, uint32_t index, uint64_t *key) {
    if (index >= ((Collection *)context)->count || !key) return 0;
    *key = (uint64_t)index + 1; return 1;
}
static PocketComponentStatus model_bind(void *context, uint32_t index, uint64_t key,
    PocketComponentRuntime *components, PocketComponentHandle item) {
    (void)context; (void)index; (void)key; (void)components; (void)item;
    /* Native materialization is authoritative. The caller composes delegates
     * after set_window returns, rather than reentering the application VM. */
    return POCKET_COMPONENT_OK;
}
static Collection *collection(Application *a, uint64_t id) {
    for (unsigned i = 0; i < LISTS; ++i) if (a->lists[i].id == id && a->lists[i].collection.impl) return &a->lists[i];
    return NULL;
}
static int reply_json(Application *a, PocketProgramValue *reply, int length) {
    if (length < 0 || (size_t)length >= sizeof(a->reply)) return 0;
    *reply = (PocketProgramValue){.kind=POCKET_PROGRAM_JSON, .text=a->reply, .length=(size_t)length};
    return 1;
}
static int command(void *context, const char *op, size_t n, const PocketProgramValue *args, PocketProgramValue *reply) {
    Application *a = context;
    int64_t v[POCKET_PROGRAM_ARGUMENT_LIMIT] = {0};
    PocketComponentHandle c = {0};
    if (!a || a->disposing || a->error || a->phase == 2) return 0;
    *reply = (PocketProgramValue){.kind=POCKET_PROGRAM_INTEGER, .integer=1};
    if (!(a->policy.capabilities & POCKET_APP_CAP_CORE)) return fail(a,"APPLICATION_CAPABILITY_CORE");
    if (!strncmp(op,"keyboard.",9) && !(a->policy.capabilities & POCKET_APP_CAP_ASCII_KEYBOARD))
        return fail(a,"APPLICATION_CAPABILITY_KEYBOARD");
    if (!strcmp(op,"component.image") && !(a->policy.capabilities & POCKET_APP_CAP_IMAGES))
        return fail(a,"APPLICATION_CAPABILITY_IMAGES");
    if (!strcmp(op, "keyboard.field")) {
        if (n != 8 || !numbers(7, args, v) || args[7].kind != POCKET_PROGRAM_TEXT || !a->keyboard_staged ||
            !between(v[0], 0, (int64_t)a->keyboard_config.field_count-1) || !between(v[1], 1, UINT32_MAX) ||
            !between(v[2], 1, 65535) || !between(v[3], 0, 3) || !between(v[4], 1, 64) ||
            !between(v[5], 0, 1) || !between(v[6], 0, 1) || args[7].length > (size_t)v[4]) return 0;
        unsigned i = (unsigned)v[0];
        if ((a->configured_fields & (1U << i)) || ((v[3] == POCKET_KEYBOARD_PASSWORD || v[3] == POCKET_KEYBOARD_PIN) && args[7].length)) return 0;
        for (size_t j = 0; j < args[7].length; ++j) if ((unsigned char)args[7].text[j] < 32 || (unsigned char)args[7].text[j] > 126) return 0;
        memcpy(a->initial[i], args[7].text, args[7].length);
        a->initial[i][args[7].length] = 0;
        a->keyboard_config.fields[i] = (PocketKeyboardField){(uint64_t)v[1], (uint64_t)v[2], (PocketKeyboardMode)v[3], (uint32_t)v[4], (uint8_t)v[5], (uint8_t)v[6], a->initial[i]};
        a->configured_fields |= 1U << i; return 1;
    }
    if (!numbers(n, args, v)) return 0;
    if (!strcmp(op, "component.create")) {
        PocketComponentHandle parent = {0};
        if (n != 9 || !between(v[0], 1, POCKET_COMPONENT_KIND_COUNT-1) || (v[1] && !component(a, v[1], &parent)) ||
            !between(v[2], -65536, 65536) || !between(v[3], -65536, 65536) || !between(v[4], 0, 65536) || !between(v[5], 0, 65536) ||
            !between(v[6], 0, 65535) || !between(v[7], 0, 65535) || !between(v[8], 0, 8)) return 0;
        if ((v[0]==POCKET_COMPONENT_IMAGE || v[8]) && !(a->policy.capabilities & POCKET_APP_CAP_IMAGES))
            return fail(a,"APPLICATION_CAPABILITY_IMAGES");
        PocketComponentProps p = pocket_component_props_default((PocketComponentKind)v[0]);
        p.style_ref = (uint64_t)v[6]; p.text_ref = (uint64_t)v[7]; p.resource_ref = (uint64_t)v[8];
        if (pocket_component_create(&a->components, (PocketComponentKind)v[0], parent, &p, &c) != POCKET_COMPONENT_OK ||
            !set_layout(a, c, (int)v[2], (int)v[3], (int)v[4], (int)v[5])) return 0;
        reply->integer = (int64_t)encoded(c.slot, c.generation); return reply->integer != 0;
    }
    if (!strcmp(op, "component.layout")) {
        return n == 5 && component(a, v[0], &c) && between(v[1], -65536, 65536) && between(v[2], -65536, 65536) &&
            between(v[3], 0, 65536) && between(v[4], 0, 65536) && set_layout(a, c, (int)v[1], (int)v[2], (int)v[3], (int)v[4]);
    }
    if (!strcmp(op, "component.text") || !strcmp(op, "component.image") || !strcmp(op, "component.style") || !strcmp(op, "component.value")) {
        if (n != 2 || !component(a, v[0], &c)) return 0;
        if (!strcmp(op, "component.text")) return between(v[1], 0, 65535) && pocket_component_set_text_ref(&a->components, c, (uint64_t)v[1]) == POCKET_COMPONENT_OK;
        if (!strcmp(op, "component.image")) return between(v[1], 0, 8) && pocket_component_set_resource_ref(&a->components, c, (uint64_t)v[1]) == POCKET_COMPONENT_OK;
        if (!strcmp(op, "component.style")) return between(v[1], 0, 65535) && pocket_component_set_style_ref(&a->components, c, (uint64_t)v[1]) == POCKET_COMPONENT_OK;
        return between(v[1], INT32_MIN, INT32_MAX) && pocket_component_set_value(&a->components, c, (int32_t)v[1]) == POCKET_COMPONENT_OK;
    }
    if (!strcmp(op, "event.bind")) {
        if (n != 3 || !component(a, v[0], &c) || v[1] <= 0 || !between(v[2], 1, 2)) return 0;
        PocketUiHandle target = root(a, c);
        Binding *found = NULL;
        for (unsigned i = 0; i < BINDINGS; ++i) {
            PocketUiSnapshot s;
            Binding *b = &a->bindings[i];
            if (b->owner && b->target.slot == target.slot && b->target.generation == target.generation) { found = b; break; }
            if (!found && (!b->owner || pocket_ui_snapshot(&a->tree, b->target, &s) != POCKET_UI_OK)) found = b;
        }
        if (!found || a->next_binding==UINT64_MAX) return 0;
        *found = (Binding){a, target, (uint64_t)v[1], ++a->next_binding, (unsigned)v[2]};
        return pocket_ui_set_event_handler(&a->tree, target, on_event, found) == POCKET_UI_OK;
    }
    if (!strcmp(op, "gesture.set")) return n == 2 && component(a, v[0], &c) && between(v[1], 0, POCKET_GESTURE_ALL) &&
        pocket_interaction_set_gestures(&a->interaction, root(a, c), (uint32_t)v[1]) == POCKET_INTERACTION_OK;
    if (!strcmp(op, "theme.define")) {
        if (n < 5 || n > 31 || !(n & 1U) || !between(v[0], 1, 16) || !between(v[1], 1, 65535) || !between(v[2], 1, 128)) return 0;
        PocketThemeToken tokens[14];
        unsigned count = (unsigned)(n-3)/2;
        for (unsigned i = 0; i < count; ++i) {
            if (!between(v[3+i*2], 1, 65535) || !between(v[4+i*2], 0, UINT32_MAX)) return 0;
            tokens[i] = (PocketThemeToken){(uint64_t)v[3+i*2], (uint32_t)v[4+i*2]};
        }
        PocketThemeDefinition t = {.id=(uint64_t)v[0], .tokens=tokens, .token_count=count};
        t.base.set_mask = POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND) | POCKET_STYLE_BIT(POCKET_STYLE_FONT_SIZE);
        t.base.fields[POCKET_STYLE_FOREGROUND] = pocket_style_token((uint64_t)v[1]);
        t.base.fields[POCKET_STYLE_FONT_SIZE] = pocket_style_literal((int32_t)v[2]);
        return pocket_style_add_theme(&a->styles, &t) == POCKET_STYLE_OK;
    }
    if (!strcmp(op, "style.rule")) {
        if (n != 4 || !between(v[0], 1, 65535) || !between(v[1], 1, 65535) || !between(v[2], 1, 65535) || !between(v[3], 0, 2048)) return 0;
        PocketStyleRule r = {0}; r.style_ref = (uint64_t)v[0];
        r.style.set_mask = POCKET_STYLE_BIT(POCKET_STYLE_BACKGROUND) | POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND) | POCKET_STYLE_BIT(POCKET_STYLE_RADIUS);
        r.style.fields[POCKET_STYLE_BACKGROUND] = pocket_style_token((uint64_t)v[1]);
        r.style.fields[POCKET_STYLE_FOREGROUND] = pocket_style_token((uint64_t)v[2]);
        r.style.fields[POCKET_STYLE_RADIUS] = pocket_style_literal((int32_t)v[3]);
        return pocket_style_add_rule(&a->styles, &r) == POCKET_STYLE_OK;
    }
    if (!strcmp(op, "theme.select")) return n == 1 && between(v[0], 1, 16) && pocket_style_set_theme(&a->styles, (uint64_t)v[0]) == POCKET_STYLE_OK;
    if (!strcmp(op, "layout.invalidate")) { if (n) return 0; a->dirty = 1; return 1; }
    if (!strcmp(op, "navigation.push") || !strcmp(op, "navigation.replace")) {
        if (n != 2 || !between(v[0], 1, 32) || !component(a, v[1], &c)) return 0;
        PocketNavigationPage page = {(uint64_t)v[0], c, 1};
        PocketNavigationStatus status = !strcmp(op, "navigation.push") ? pocket_navigation_push(&a->navigation, &page) : pocket_navigation_replace(&a->navigation, &page);
        if (status != POCKET_NAV_OK) return 0;
        a->dirty = 1;
        if (!a->interaction.impl) {
            PocketInteractionConfig ic = {.tree=&a->tree, .layout=&a->layout, .components=&a->components, .overlays=&a->overlays, .scene_root=root(a, c)};
            return pocket_interaction_init(&a->interaction, &ic) == POCKET_INTERACTION_OK;
        }
        return 1;
    }
    if (!strcmp(op, "navigation.pop")) { if (n) return 0; a->dirty = 1; return pocket_navigation_pop(&a->navigation) == POCKET_NAV_OK; }
    if (!strcmp(op, "overlay.present")) {
        if (n != 4 || v[0] <= 0 || !between(v[1], 1, 32) || !component(a, v[2], &c) || !between(v[3], 1, POCKET_OVERLAY_LOADING)) return 0;
        if(v[3]==POCKET_OVERLAY_KEYBOARD && !(a->policy.capabilities & POCKET_APP_CAP_ASCII_KEYBOARD))
            return fail(a,"APPLICATION_CAPABILITY_KEYBOARD");
        PocketOverlaySpec s = {.id=(uint64_t)v[0], .owner_route=(uint64_t)v[1], .kind=(PocketOverlayKind)v[3], .root=c,
            .focus_token=encoded(c.slot,c.generation), .owns_root=1, .captures_input=1, .captures_focus=1};
        if (pocket_overlay_present(&a->overlays, &s) != POCKET_OVERLAY_OK) return 0;
        a->dirty = 1; return remember_layer(a, s.id, 0);
    }
    if (!strcmp(op, "overlay.dismiss")) { if (n != 1 || v[0] <= 0) return 0; a->dirty = 1; return pocket_overlay_dismiss(&a->overlays, (uint64_t)v[0]) == POCKET_OVERLAY_OK; }
    if (!strcmp(op, "input.cancel")) { if (n) return 0; if (a->interaction.impl) pocket_interaction_cancel_all(&a->interaction, a->now); return 1; }
    if (!strcmp(op, "input.active")) {
        PocketInteractionSnapshot s;
        if (n || pocket_interaction_snapshot(&a->interaction, &s) != POCKET_INTERACTION_OK) return 0;
        reply->integer = s.active_pointers; return 1;
    }
    if (!strcmp(op, "signal.create")) {
        PocketReactiveHandle h;
        if (n != 1 || pocket_reactive_signal(&a->reactive, pocket_value_i64(v[0]), &h) != POCKET_REACTIVE_OK) return 0;
        reply->integer = (int64_t)encoded(h.slot, h.generation); return reply->integer != 0;
    }
    if (!strcmp(op, "signal.set")) return n == 2 && v[0] > 0 && pocket_reactive_set(&a->reactive, signal_handle(v[0]), pocket_value_i64(v[1])) == POCKET_REACTIVE_OK;
    if (!strcmp(op, "signal.bind")) {
        if(n==3 && v[2]==POCKET_BIND_RESOURCE_REF && !(a->policy.capabilities & POCKET_APP_CAP_IMAGES))
            return fail(a,"APPLICATION_CAPABILITY_IMAGES");
        PocketReactiveSubscription subscription;
        return n == 3 && v[0] > 0 && component(a, v[1], &c) && between(v[2], 1, 7) &&
            pocket_reactive_bind_component(&a->reactive, signal_handle(v[0]), c, (PocketBindingTarget)v[2], &subscription) == POCKET_REACTIVE_OK;
    }
    if (!strcmp(op, "list.create")) {
        if (n != 4 || !component(a, v[0], &c) || !between(v[1], 1, 4096) || !between(v[2], 1, 32) || !between(v[3], 1, POCKET_COMPONENT_KIND_COUNT-1)) return 0;
        if(v[3]==POCKET_COMPONENT_IMAGE && !(a->policy.capabilities & POCKET_APP_CAP_IMAGES))
            return fail(a,"APPLICATION_CAPABILITY_IMAGES");
        Collection *list = NULL;
        for (unsigned i = 0; i < LISTS; ++i) if (!a->lists[i].collection.impl) { list = &a->lists[i]; break; }
        if (!list || a->next_list >= SAFE_INTEGER) return 0;
        list->count = (uint32_t)v[1]; list->parent = c;
        PocketVirtualCollectionConfig config = {.components=&a->components, .parent=c, .item_kind=(PocketComponentKind)v[3], .max_pool=(uint32_t)v[2], .overscan=0,
            .model={list, model_count, model_key, model_bind}};
        if (pocket_virtual_collection_init(&list->collection, &config) != POCKET_MODEL_OK) return 0;
        list->id = ++a->next_list; reply->integer = (int64_t)list->id; return 1;
    }
    if (!strcmp(op, "list.window")) {
        Collection *list;
        if (n != 3 || v[0] <= 0 || !(list=collection(a,(uint64_t)v[0])) || !between(v[1],0,list->count-1) || !between(v[2],1,32) || v[1]+v[2] > list->count) return 0;
        if (pocket_virtual_collection_set_window(&list->collection, (uint32_t)v[1], (uint32_t)v[2]) != POCKET_MODEL_OK) return 0;
        int length = snprintf(a->reply, sizeof(a->reply), "[");
        for (int64_t i=0; i<v[2]; ++i) {
            if (pocket_virtual_collection_component_for_key(&list->collection,(uint64_t)(v[1]+i+1),&c) != POCKET_MODEL_OK) return 0;
            uint64_t handle = encoded(c.slot,c.generation);
            if (!handle || length < 0 || (size_t)length >= sizeof(a->reply)) return 0;
            int added = snprintf(a->reply+length,sizeof(a->reply)-(size_t)length,"%s[%" PRId64 ",%" PRIu64 "]",i?",":"",v[1]+i,handle);
            if (added < 0 || (size_t)added >= sizeof(a->reply)-(size_t)length) return 0;
            length += added;
        }
        if ((size_t)length+2 > sizeof(a->reply)) return 0;
        a->reply[length++]=']'; a->reply[length]=0; a->dirty=1;
        return reply_json(a,reply,length);
    }
    if (!strcmp(op, "list.dispose")) {
        Collection *list;
        if (n!=1 || v[0]<=0 || !(list=collection(a,(uint64_t)v[0]))) return 0;
        pocket_virtual_collection_dispose(&list->collection); memset(list,0,sizeof(*list)); a->dirty=1; return 1;
    }
    if (!strcmp(op, "keyboard.begin")) {
        if (n!=9 || a->keyboard.impl || a->keyboard_staged || v[0]<=0 || !between(v[1],1,32) || !between(v[8],1,POCKET_KEYBOARD_MAX_FIELDS)) return 0;
        for (unsigned i=2;i<8;++i) if (!between(v[i],1,65535)) return 0;
        PocketNavigationPage page;
        if (pocket_navigation_top(&a->navigation,&page)!=POCKET_NAV_OK || page.route_id!=(uint64_t)v[1]) return 0;
        erase(a->initial,sizeof(a->initial));
        a->keyboard_config=(PocketKeyboardConfig){.tree=&a->tree,.layout=&a->layout,.components=&a->components,.interaction=&a->interaction,.overlays=&a->overlays,
            .height=a->height,.field_count=(uint32_t)v[8],.overlay_id=(uint64_t)v[0],.owner_route=(uint64_t)v[1],.page_style=(uint64_t)v[2],.text_style=(uint64_t)v[3],
            .muted_style=(uint64_t)v[4],.field_style=(uint64_t)v[5],.key_style=(uint64_t)v[6],.primary_style=(uint64_t)v[7]};
        a->configured_fields=0; a->keyboard_staged=1; return 1;
    }
    if (!strcmp(op, "keyboard.show")) {
        if (n || !a->keyboard_staged || a->configured_fields != (1U<<a->keyboard_config.field_count)-1U || !pocket_keyboard_open(&a->keyboard,&a->keyboard_config)) return 0;
        a->keyboard_staged=0; ++a->stats.editor_opens;
        return remember_layer(a,a->keyboard_config.overlay_id,1);
    }
    if (!strcmp(op, "keyboard.step")) {
        if (n) return 0;
        if (!a->keyboard.impl) { reply->integer=-1; return 1; }
        PocketKeyboardSnapshot s;
        if (!pocket_keyboard_step(&a->keyboard,a->now) || !pocket_keyboard_snapshot(&a->keyboard,&s)) return 0;
        reply->integer=s.result; return 1;
    }
    if (!strcmp(op, "keyboard.result")) {
        if (n!=1 || !a->keyboard.impl || !between(v[0],0,(int64_t)a->keyboard_config.field_count-1)) return 0;
        PocketKeyboardMode mode=a->keyboard_config.fields[v[0]].mode;
        if (mode==POCKET_KEYBOARD_PASSWORD || mode==POCKET_KEYBOARD_PIN) return 0;
        size_t length=0;
        if (pocket_keyboard_copy_result(&a->keyboard,(uint32_t)v[0],a->reply,sizeof(a->reply),&length)!=POCKET_TEXT_OK) return 0;
        *reply=(PocketProgramValue){.kind=POCKET_PROGRAM_TEXT,.text=a->reply,.length=length}; return 1;
    }
    if (!strcmp(op, "keyboard.close")) {
        PocketKeyboardSnapshot s;
        if (n || !a->keyboard.impl || !pocket_keyboard_snapshot(&a->keyboard,&s)) return 0;
        if (s.result==POCKET_KEYBOARD_CONFIRMED) ++a->stats.editor_confirms; else ++a->stats.editor_cancels;
        pocket_keyboard_dispose(&a->keyboard); erase(a->initial,sizeof(a->initial)); erase(a->reply,sizeof(a->reply)); a->dirty=1; return 1;
    }
    if (!strcmp(op, "frame.state")) {
        if (n!=13) return 0;
        for (unsigned i=0;i<8;++i) if (!between(v[i],0,UINT32_MAX)) return 0;
        if (!between(v[0],0,4096) || !between(v[1],0,4096) || !between(v[2],0,4096) || !between(v[3],0,5) || !between(v[4],0,15) || !between(v[5],0,100) ||
            !between(v[8],-65536,65536) || !between(v[9],0,1) || !between(v[10],0,1) || !between(v[11],0,UINT32_MAX) || !between(v[12],0,1)) return 0;
        a->stats.selected=(unsigned)v[0]; a->stats.first=(unsigned)v[1]; a->stats.item_count=(unsigned)v[2];
        a->stats.locale=(unsigned)v[3]; a->stats.theme=(unsigned)v[4]; a->stats.progress=(unsigned)v[5]; a->stats.completed=(unsigned)v[6]; a->stats.actions=(unsigned)v[7];
        a->stats.scroll_x=(int)v[8]; a->stats.scroll_dragging=(unsigned)v[9]; a->stats.scroll_settling=(unsigned)v[10]; a->background=(uint32_t)v[11]; a->media_ready=(unsigned)v[12];
        return 1;
    }
    return 0;
}
int pocket_application_open(PocketApplication *out, unsigned height, unsigned items, const char *source, size_t length) {
    const PocketApplicationPolicy policy={POCKET_APP_MAX_HEAP,POCKET_APP_CAP_ALL};
    return pocket_application_open_policy(out,height,items,source,length,&policy);
}
int pocket_application_open_policy(PocketApplication *out, unsigned height, unsigned items,
                                   const char *source, size_t length, const PocketApplicationPolicy *policy) {
    if (!out || out->impl || (height!=600 && height!=800) || items>4096 || !source || !length ||
        !policy || policy->heap_bytes<1024u*1024u || policy->heap_bytes>POCKET_APP_MAX_HEAP ||
        !(policy->capabilities&POCKET_APP_CAP_CORE) || (policy->capabilities&~POCKET_APP_CAP_ALL)) return 0;
    Application *a=calloc(1,sizeof(*a));
    if (!a) return 0;
    out->impl=a; a->height=height; a->dirty=1; a->policy=*policy;
    PocketUiTreeConfig tc={.initial_capacity=128,.update_queue_capacity=64,.update_budget=64};
    PocketLayoutConfig lc={.tree=&a->tree,.record_capacity=256};
    PocketStyleRuntimeConfig sc={.theme_capacity=2,.token_capacity=32,.rule_capacity=32};
    PocketComponentRuntimeConfig cc={.tree=&a->tree,.layout=&a->layout,.styles=&a->styles,.capacity=256};
    PocketOverlayConfig oc={.components=&a->components,.capacity=OVERLAYS};
    PocketNavigationConfig nc={.components=&a->components,.capacity=4,.owner_cleanup=pocket_overlay_navigation_cleanup,.owner_cleanup_context=&a->overlays};
    PocketReactiveConfig rc={.node_capacity=16,.effect_capacity=8,.binding_capacity=16,.default_flush_budget=64,.components=&a->components};
    if (pocket_ui_tree_init(&a->tree,&tc)!=POCKET_UI_OK || pocket_layout_init(&a->layout,&lc)!=POCKET_UI_OK ||
        pocket_style_runtime_init(&a->styles,&sc)!=POCKET_STYLE_OK || pocket_component_runtime_init(&a->components,&cc)!=POCKET_COMPONENT_OK ||
        pocket_overlay_init(&a->overlays,&oc)!=POCKET_OVERLAY_OK || pocket_navigation_init(&a->navigation,&nc)!=POCKET_NAV_OK || pocket_reactive_init(&a->reactive,&rc)!=POCKET_REACTIVE_OK) goto failed;
    PocketProgramConfig config=pocket_program_config(); config.context=a; config.command=command;
    config.memory_limit=a->policy.heap_bytes;
    a->phase=1;
    if (!pocket_program_open(&a->program,&config,source,length)) goto failed;
    char input[96], result[16]; size_t result_length=0;
    snprintf(input,sizeof(input),"{\"width\":1024,\"height\":%u,\"items\":%u}",height,items);
    if (!invoke(a,"start",input,result,sizeof(result),&result_length) || result_length!=4 || memcmp(result,"true",4) || !a->interaction.impl || !sync_layout(a)) goto failed;
    a->phase=0; return 1;
failed:
    pocket_application_close(out); return 0;
}
int pocket_application_policy_snapshot(const PocketApplication *out,PocketApplicationPolicy *policy) {
    const Application *a=out?out->impl:NULL;
    if(policy)memset(policy,0,sizeof(*policy));
    if(!a||!policy)return 0;
    *policy=a->policy;return 1;
}
int pocket_application_step(PocketApplication *out, uint64_t ms) {
    Application *a=out?out->impl:NULL;
    if (!a || a->error || ms<a->now || ms>SAFE_INTEGER) return 0;
    a->now=ms;
    if (!drain_events(a) || pocket_interaction_tick(&a->interaction,ms)!=POCKET_INTERACTION_OK) return fail(a,"APPLICATION_INPUT_TICK");
    char input[80], result[16]; size_t length=0;
    snprintf(input,sizeof(input),"{\"time\":%" PRIu64 "}",ms);
    a->phase=1;
    int ok=invoke(a,"tick",input,result,sizeof(result),&length);
    a->phase=0;
    if (!ok || length!=4 || memcmp(result,"true",4)) return fail(a,"APPLICATION_TICK_RESULT");
    PocketReactiveFlushStats flush;
    if (pocket_reactive_flush(&a->reactive,64,&flush)!=POCKET_REACTIVE_OK || !sync_layout(a) || pocket_interaction_tick(&a->interaction,ms)!=POCKET_INTERACTION_OK) return fail(a,"APPLICATION_FLUSH");
    return !a->error;
}
int pocket_application_prepare_input(PocketApplication *out) {
    Application *a=out?out->impl:NULL; return a&&!a->error&&drain_events(a);
}
PocketInteractionRuntime *pocket_application_interaction(PocketApplication *out) {
    Application *a=out?out->impl:NULL; return a?&a->interaction:NULL;
}
int pocket_application_scene(PocketApplication *out, PocketScene *scene) {
    Application *a=out?out->impl:NULL;
    PocketNavigationPage page; PocketInteractionSnapshot input;
    if (!a || a->error || !scene || pocket_navigation_top(&a->navigation,&page)!=POCKET_NAV_OK || pocket_interaction_snapshot(&a->interaction,&input)!=POCKET_INTERACTION_OK) return 0;
    PocketSceneSource source={&a->tree,&a->components,&a->layout,&a->styles};
    if (!pocket_scene_begin(scene,1024,a->height,a->stats.locale,a->background,a->media_ready && !input.active_pointers && !pocket_overlay_count(&a->overlays)) ||
        !pocket_scene_append(scene,&source,root(a,page.root))) return 0;
    prune_layers(a);
    PocketOverlaySnapshot layers[OVERLAYS]; unsigned count=0;
    for (unsigned i=0;i<OVERLAYS;++i) if (a->layers[i].id) {
        PocketOverlaySnapshot s;
        if (pocket_overlay_snapshot(&a->overlays,a->layers[i].id,&s)!=POCKET_OVERLAY_OK) return 0;
        unsigned j=count++;
        while (j && layers[j-1].z_order>s.z_order) { layers[j]=layers[j-1]; --j; }
        layers[j]=s;
    }
    for (unsigned i=0;i<count;++i) if (!pocket_scene_append(scene,&source,root(a,layers[i].spec.root))) return 0;
    if(!(a->policy.capabilities&POCKET_APP_CAP_IMAGES)){
        for(uint32_t i=0;i<scene->count;++i)if(scene->records[i].resource_ref)
            return fail(a,"APPLICATION_CAPABILITY_IMAGES");
    }
    return 1;
}
int pocket_application_stats(const PocketApplication *out, PocketApplicationStats *stats) {
    const Application *a=out?out->impl:NULL; PocketNavigationPage page;
    if (!a || a->error || !stats || pocket_navigation_top(&a->navigation,&page)!=POCKET_NAV_OK) return 0;
    *stats=a->stats; stats->page=(unsigned)page.route_id;
    stats->modal=(unsigned)pocket_overlay_count(&a->overlays); stats->nodes=(unsigned)pocket_ui_live_count(&a->tree);
    stats->pool=stats->peak_pool=0; stats->recycled=0;
    for (unsigned i=0;i<LISTS;++i) if (a->lists[i].collection.impl) {
        PocketVirtualCollectionStats s;
        if (pocket_virtual_collection_stats(&a->lists[i].collection,&s)!=POCKET_MODEL_OK) return 0;
        stats->pool+=s.pool_size; stats->peak_pool+=s.peak_pool_size; stats->recycled+=s.recycle_count;
    }
    stats->editor_active=a->keyboard.impl!=NULL; return 1;
}
int pocket_application_keyboard_snapshot(const PocketApplication *out, PocketKeyboardSnapshot *snapshot) {
    const Application *a=out?out->impl:NULL;
    return a && a->keyboard.impl && pocket_keyboard_snapshot(&a->keyboard,snapshot);
}
int pocket_application_snapshot_allowed(const PocketApplication *out) {
    const Application *a=out?out->impl:NULL; return a && !a->error && !a->keyboard.impl;
}
const char *pocket_application_error(const PocketApplication *out) {
    const Application *a=out?out->impl:NULL; return a?a->error:"APPLICATION_NOT_OPEN";
}
void pocket_application_close(PocketApplication *out) {
    Application *a=out?out->impl:NULL;
    if (!a) return;
    a->disposing=1;
    pocket_keyboard_dispose(&a->keyboard); pocket_interaction_dispose(&a->interaction);
    pocket_program_close(&a->program); pocket_reactive_dispose(&a->reactive);
    for (unsigned i=0;i<LISTS;++i) pocket_virtual_collection_dispose(&a->lists[i].collection);
    pocket_overlay_dispose(&a->overlays); pocket_navigation_dispose(&a->navigation);
    pocket_component_runtime_dispose(&a->components); pocket_style_runtime_dispose(&a->styles);
    pocket_layout_dispose(&a->layout); pocket_ui_tree_dispose(&a->tree);
    erase(a,sizeof(*a)); free(a); out->impl=NULL;
}
