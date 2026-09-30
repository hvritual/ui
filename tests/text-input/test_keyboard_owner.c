#include "hosts/linux/text-input/keyboard.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"KEYBOARD_OWNER_FAIL line=%d %s\n",__LINE__,#x);return 0;}}while(0)
typedef struct {
    PocketUiTree tree;PocketLayoutContext layout;PocketComponentRuntime components;
    PocketOverlayManager overlays;PocketInteractionRuntime interaction;PocketKeyboard keyboard;
    PocketComponentHandle scene;PocketKeyboardConfig config;uint64_t clock;
} Fixture;
static int init(Fixture *f){
    memset(f,0,sizeof(*f));
    CHECK(pocket_ui_tree_init(&f->tree,&(PocketUiTreeConfig){.initial_capacity=256,.update_queue_capacity=64,.update_budget=64})==POCKET_UI_OK);
    CHECK(pocket_layout_init(&f->layout,&(PocketLayoutConfig){.tree=&f->tree,.record_capacity=512})==POCKET_UI_OK);
    CHECK(pocket_component_runtime_init(&f->components,&(PocketComponentRuntimeConfig){.tree=&f->tree,.layout=&f->layout,.capacity=256})==POCKET_COMPONENT_OK);
    CHECK(pocket_component_create(&f->components,POCKET_COMPONENT_VIEW,(PocketComponentHandle){0},NULL,&f->scene)==POCKET_COMPONENT_OK);
    PocketComponentSnapshot scene;CHECK(pocket_component_snapshot(&f->components,f->scene,&scene)==POCKET_COMPONENT_OK);
    CHECK(pocket_layout_run(&f->layout,scene.root,1024,600)==POCKET_UI_OK);
    CHECK(pocket_overlay_init(&f->overlays,&(PocketOverlayConfig){.components=&f->components,.capacity=8})==POCKET_OVERLAY_OK);
    CHECK(pocket_interaction_init(&f->interaction,&(PocketInteractionConfig){.tree=&f->tree,.layout=&f->layout,.components=&f->components,.overlays=&f->overlays,.scene_root=scene.root})==POCKET_INTERACTION_OK);
    f->config=(PocketKeyboardConfig){.tree=&f->tree,.layout=&f->layout,.components=&f->components,.interaction=&f->interaction,
      .overlays=&f->overlays,.height=600,.field_count=2,.overlay_id=900,.owner_route=1,
      .page_style=1,.text_style=2,.muted_style=3,.field_style=4,.key_style=5,.primary_style=6,
      .fields={{1,205,POCKET_KEYBOARD_ASCII,64,1,0,""},{2,206,POCKET_KEYBOARD_NUMBER,16,1,0,""}}};
    return 1;
}
static int step(Fixture *f){f->clock+=20;CHECK(pocket_interaction_tick(&f->interaction,f->clock)==POCKET_INTERACTION_OK);CHECK(pocket_keyboard_step(&f->keyboard,f->clock));return 1;}
static int tap(Fixture *f,int x,int y){
    f->clock+=20;CHECK(pocket_interaction_pointer(&f->interaction,&(PocketPointerEvent){1,POCKET_POINTER_DOWN,x,y,f->clock})==POCKET_INTERACTION_OK);CHECK(step(f));
    f->clock+=20;CHECK(pocket_interaction_pointer(&f->interaction,&(PocketPointerEvent){1,POCKET_POINTER_UP,x,y,f->clock})==POCKET_INTERACTION_OK);CHECK(step(f));return 1;
}
static int value(Fixture *f,unsigned index,const char *expected){
    char out[65];size_t n;CHECK(pocket_keyboard_copy_result(&f->keyboard,index,out,sizeof(out),&n)==POCKET_TEXT_OK);
    CHECK(n==strlen(expected)&&memcmp(out,expected,n)==0);memset(out,0,sizeof(out));return 1;
}
static int clean(Fixture *f){
    pocket_keyboard_dispose(&f->keyboard);CHECK(pocket_component_live_count(&f->components)==1);
    CHECK(pocket_ui_live_count(&f->tree)==1);CHECK(pocket_overlay_count(&f->overlays)==0);
    PocketInteractionSnapshot s;CHECK(pocket_interaction_snapshot(&f->interaction,&s)==POCKET_INTERACTION_OK&&!s.active_pointers);
    return 1;
}
static void end(Fixture *f){
    pocket_keyboard_dispose(&f->keyboard);pocket_interaction_dispose(&f->interaction);pocket_overlay_dispose(&f->overlays);
    pocket_component_runtime_dispose(&f->components);pocket_layout_dispose(&f->layout);pocket_ui_tree_dispose(&f->tree);
}
int test_keyboard_owner(void){
    Fixture f;CHECK(init(&f));PocketKeyboardSnapshot s;
    f.config.fields[0].max_chars=2;CHECK(pocket_keyboard_open(&f.keyboard,&f.config));
    char blocked[8];size_t n;CHECK(pocket_keyboard_copy_result(&f.keyboard,0,blocked,sizeof(blocked),&n)==POCKET_TEXT_INVALID_ARGUMENT);
    CHECK(tap(&f,59,370));CHECK(tap(&f,59,370));CHECK(tap(&f,59,370));
    CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.last_edit_status==POCKET_TEXT_LIMIT);
    CHECK(tap(&f,900,40));CHECK(value(&f,0,"qq"));CHECK(clean(&f));

    f.config.fields[0].enabled=0;f.config.fields[1].read_only=1;f.config.fields[1].initial="123";
    CHECK(pocket_keyboard_open(&f.keyboard,&f.config));CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.active_field==1);
    /* Only second field is focusable. The disabled first field is not admitted. */
    CHECK(tap(&f,59,370));CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.last_edit_status==POCKET_TEXT_READ_ONLY);
    CHECK(tap(&f,900,40));CHECK(value(&f,1,"123"));CHECK(clean(&f));

    f.config.fields[0].enabled=1;f.config.fields[0].max_chars=64;f.config.fields[1].read_only=0;
    f.config.fields[1].initial="bad-number";CHECK(!pocket_keyboard_open(&f.keyboard,&f.config));CHECK(clean(&f));
    f.config.fields[1].initial="";f.config.fields[1].field_id=1;CHECK(!pocket_keyboard_open(&f.keyboard,&f.config));CHECK(clean(&f));
    f.config.fields[1].field_id=2;
    /* An overlay ID may be reused after the owner dismissed this keyboard.
     * Old cleanup must never remove the new owner's root or input contact. */
    CHECK(pocket_keyboard_open(&f.keyboard,&f.config));
    CHECK(pocket_overlay_dismiss(&f.overlays,f.config.overlay_id)==POCKET_OVERLAY_OK);
    PocketComponentHandle replacement={0};
    CHECK(pocket_component_create(&f.components,POCKET_COMPONENT_MODAL,(PocketComponentHandle){0},NULL,&replacement)==POCKET_COMPONENT_OK);
    CHECK(pocket_overlay_present(&f.overlays,&(PocketOverlaySpec){.id=f.config.overlay_id,.kind=POCKET_OVERLAY_MODAL,
        .root=replacement,.owns_root=1,.captures_input=1})==POCKET_OVERLAY_OK);
    PocketComponentHandle replacement_button={0};
    CHECK(pocket_component_create(&f.components,POCKET_COMPONENT_BUTTON,replacement,NULL,&replacement_button)==POCKET_COMPONENT_OK);
    PocketLayoutSpec button_layout=pocket_layout_spec_default();
    button_layout.width=(PocketLength){POCKET_LENGTH_PX,100};button_layout.height=(PocketLength){POCKET_LENGTH_PX,100};
    CHECK(pocket_component_set_layout(&f.components,replacement_button,&button_layout)==POCKET_COMPONENT_OK);
    PocketComponentSnapshot replacement_root;
    CHECK(pocket_component_snapshot(&f.components,replacement,&replacement_root)==POCKET_COMPONENT_OK);
    CHECK(pocket_layout_run(&f.layout,replacement_root.root,1024,600)==POCKET_UI_OK);
    f.clock+=20;
    CHECK(pocket_interaction_pointer(&f.interaction,&(PocketPointerEvent){7,POCKET_POINTER_DOWN,20,20,f.clock})==POCKET_INTERACTION_OK);
    CHECK(step(&f));CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.result==POCKET_KEYBOARD_CANCELLED);
    PocketInteractionSnapshot replacement_input;
    CHECK(pocket_interaction_snapshot(&f.interaction,&replacement_input)==POCKET_INTERACTION_OK&&replacement_input.active_pointers==1);
    f.clock+=20;
    CHECK(pocket_interaction_pointer(&f.interaction,&(PocketPointerEvent){7,POCKET_POINTER_UP,20,20,f.clock})==POCKET_INTERACTION_OK);
    PocketOverlaySnapshot replacement_snapshot;
    CHECK(pocket_overlay_snapshot(&f.overlays,f.config.overlay_id,&replacement_snapshot)==POCKET_OVERLAY_OK);
    CHECK(replacement_snapshot.spec.root.slot==replacement.slot&&replacement_snapshot.spec.root.generation==replacement.generation);
    pocket_keyboard_dispose(&f.keyboard);
    CHECK(pocket_component_live_count(&f.components)==3);
    /* Duplicate-ID open failure must also leave that unrelated modal alone. */
    CHECK(!pocket_keyboard_open(&f.keyboard,&f.config));
    CHECK(pocket_overlay_snapshot(&f.overlays,f.config.overlay_id,&replacement_snapshot)==POCKET_OVERLAY_OK);
    CHECK(pocket_component_live_count(&f.components)==3);
    CHECK(pocket_overlay_dismiss(&f.overlays,f.config.overlay_id)==POCKET_OVERLAY_OK);CHECK(clean(&f));

    /* External component destruction precedes stale overlay cleanup sometimes. */
    CHECK(pocket_keyboard_open(&f.keyboard,&f.config));
    CHECK(pocket_overlay_snapshot(&f.overlays,f.config.overlay_id,&replacement_snapshot)==POCKET_OVERLAY_OK);
    CHECK(pocket_component_destroy(&f.components,replacement_snapshot.spec.root)==POCKET_COMPONENT_OK);
    f.clock+=20;CHECK(pocket_keyboard_step(&f.keyboard,f.clock));
    CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.result==POCKET_KEYBOARD_CANCELLED);CHECK(clean(&f));
    for(unsigned j=0;j<100;j++){
        CHECK(pocket_keyboard_open(&f.keyboard,&f.config));CHECK(tap(&f,59,370));CHECK(tap(&f,720,40));
        CHECK(pocket_keyboard_snapshot(&f.keyboard,&s)&&s.result==POCKET_KEYBOARD_CANCELLED);
        CHECK(pocket_keyboard_copy_result(&f.keyboard,0,blocked,sizeof(blocked),&n)==POCKET_TEXT_INVALID_ARGUMENT);CHECK(clean(&f));
    }
    end(&f);puts("KEYBOARD_OWNER_OK limits readonly disabled cancellation 100-lifecycles");return 1;
}
