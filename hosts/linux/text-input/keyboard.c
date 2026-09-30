#include "keyboard.h"
#include <stdlib.h>
#include <string.h>

#define VIEW_CELLS 36U
#define PREVIEW_CELLS 8U
#define KEY_CELLS 48U
#define BINDINGS 72U
#define CELL_WIDTH 24
/* Same contact-owned, bounded-repeat principles as pinned keyboard-touch.ts.
 * F6 supplies hit testing, cancellation and long press; no second gesture tree. */
#define REPEAT_MS 85U

enum { LABEL_TITLE=200,LABEL_HELP,LABEL_CANCEL,LABEL_CONFIRM,LABEL_ENTRY,
       LABEL_NAME,LABEL_NUMBER,LABEL_PASSWORD,LABEL_PIN,LABEL_HOME,LABEL_LEFT,
       LABEL_RIGHT,LABEL_END,LABEL_ALL,LABEL_CLEAR,LABEL_SHIFT,LABEL_CAPS,
       LABEL_SYMBOLS,LABEL_LETTERS,LABEL_BACKSPACE,LABEL_SPACE,LABEL_NEXT,
       LABEL_HIDE,LABEL_SHOW,LABEL_LIMIT,LABEL_POLICY,LABEL_PAUSED };
enum { CMD_INSERT=1,CMD_BACKSPACE,CMD_LEFT,CMD_RIGHT,CMD_HOME,CMD_END,
       CMD_ALL,CMD_CLEAR,CMD_SHIFT,CMD_CAPS,CMD_MODE,CMD_NEXT,CMD_HIDE,
       CMD_CONFIRM,CMD_CANCEL,CMD_CARET,CMD_FIELD };
typedef struct Impl Impl;
typedef struct { Impl *owner; PocketComponentHandle component; unsigned command,value; } Binding;
typedef struct {
    int active,released,cancelled,repeating;
    uint32_t id;unsigned binding;int x,y;
    PocketTextToken token;
    uint64_t next_repeat;
} Press;
struct Impl {
    PocketKeyboardConfig config;
    PocketTextSession sessions[POCKET_KEYBOARD_MAX_FIELDS];
    PocketComponentHandle root,help,fields[4],previews[4][PREVIEW_CELLS];
    PocketComponentHandle band,selection,caret,glyphs[VIEW_CELLS],keys[KEY_CELLS];
    PocketComponentHandle key_group,mode_key,space_key,shift_key,caps_key,hide_key;
    Binding bindings[BINDINGS];unsigned binding_count;
    unsigned active,view_start,mode,shift,caps;
    int focused,visible,dirty,failed,presented;
    PocketKeyboardResult result;
    PocketTextStatus edit_status;
    Press press;
    int pending;unsigned pending_command,pending_value;
    PocketTextToken pending_token;
    uint64_t now;
};
static int same_ui(PocketUiHandle a,PocketUiHandle b){return a.slot==b.slot&&a.generation==b.generation;}
static int same_component(PocketComponentHandle a,PocketComponentHandle b){
    return a.slot==b.slot&&a.generation==b.generation;
}
static int same_token(PocketTextToken a,PocketTextToken b){
    return a.field_id==b.field_id&&a.session_id==b.session_id&&a.focus_generation==b.focus_generation&&
           a.engine_generation==b.engine_generation&&a.revision==b.revision;
}
static PocketUiHandle root_of(Impl *i,PocketComponentHandle c){
    PocketComponentSnapshot s;
    if(pocket_component_snapshot(i->config.components,c,&s)!=POCKET_COMPONENT_OK)return (PocketUiHandle){0};
    return s.root;
}
static void wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static int layout(Impl *i,PocketComponentHandle c,int x,int y,int w,int h){
    PocketLayoutSpec s=pocket_layout_spec_default();s.mode=POCKET_LAYOUT_ABSOLUTE;
    s.width=(PocketLength){POCKET_LENGTH_PX,w};s.height=(PocketLength){POCKET_LENGTH_PX,h};
    s.offset_x=(PocketLength){POCKET_LENGTH_PX,x};s.offset_y=(PocketLength){POCKET_LENGTH_PX,y};
    s.overflow=POCKET_OVERFLOW_CLIP;
    return pocket_component_set_layout(i->config.components,c,&s)==POCKET_COMPONENT_OK;
}
static PocketComponentHandle make(Impl *i,PocketComponentKind kind,PocketComponentHandle parent,
                                 int x,int y,int w,int h,uint64_t style,uint64_t text){
    PocketComponentHandle c={0};PocketComponentProps p=pocket_component_props_default(kind);
    p.style_ref=style;p.text_ref=text;
    if(pocket_component_create(i->config.components,kind,parent,&p,&c)!=POCKET_COMPONENT_OK||
       !layout(i,c,x,y,w,h))i->failed=1;
    return c;
}
static int text(Impl *i,PocketComponentHandle c,uint64_t ref){
    return pocket_component_set_text_ref(i->config.components,c,ref)==POCKET_COMPONENT_OK;
}
static int visible(Impl *i,PocketComponentHandle c,int show){
    return pocket_component_set_visible(i->config.components,c,show)==POCKET_COMPONENT_OK;
}
static int disabled(Impl *i,PocketComponentHandle c,int off){
    return pocket_component_set_disabled(i->config.components,c,off)==POCKET_COMPONENT_OK;
}
static int focused(Impl *i,PocketTextSnapshot *out){
    PocketInteractionSnapshot f;
    return i->focused&&pocket_interaction_snapshot(i->config.interaction,&f)==POCKET_INTERACTION_OK&&
           same_ui(f.focused,root_of(i,i->fields[i->active]))&&
           pocket_text_snapshot(&i->sessions[i->active],out)==POCKET_TEXT_OK&&out->active;
}
static void lose_focus(Impl *i){
    if(i->focused)(void)pocket_text_blur(&i->sessions[i->active]);
    i->focused=0;i->pending=0;memset(&i->press,0,sizeof(i->press));i->dirty=1;
}
static void queue(Impl *i,unsigned command,unsigned value,PocketTextToken token){
    /* At most one semantic command from a snapshot; never replay late actions. */
    if(i->pending)return;
    i->pending=1;i->pending_command=command;i->pending_value=value;i->pending_token=token;
}
static PocketUiEventAction event(void *context,PocketUiEvent *e){
    Binding *b=context;Impl *i=b->owner;
    if(e->phase!=POCKET_UI_EVENT_TARGET||i->result!=POCKET_KEYBOARD_EDITING)return POCKET_UI_EVENT_CONTINUE;
    if(b->command==CMD_FIELD){
        if(e->type==POCKET_UI_EVENT_FOCUS_LOST){if(i->focused&&i->active==b->value)lose_focus(i);}
        else if(e->type==POCKET_UI_EVENT_FOCUS_GAINED){
            lose_focus(i);i->active=b->value;i->view_start=0;
            if(pocket_text_focus(&i->sessions[i->active])!=POCKET_TEXT_OK)i->failed=1;
            else i->focused=1;
        }else if(e->type==POCKET_UI_EVENT_KEY_ACTION){
            PocketTextSnapshot s;
            if(focused(i,&s)){
                unsigned cmd=e->data==POCKET_KEY_ACTION_BACK?CMD_CANCEL:e->data==POCKET_KEY_ACTION_NEXT?CMD_NEXT:
                    e->data==POCKET_KEY_ACTION_ACCEPT?CMD_CONFIRM:e->data==POCKET_KEY_ACTION_LEFT?CMD_LEFT:
                    e->data==POCKET_KEY_ACTION_RIGHT?CMD_RIGHT:0;
                if(cmd){queue(i,cmd,0,s.token);return POCKET_UI_EVENT_CONSUME;}
            }
        }
        return POCKET_UI_EVENT_CONTINUE;
    }
    if(e->type==POCKET_UI_EVENT_POINTER_CANCEL||e->type==POCKET_UI_EVENT_GESTURE_CANCEL){
        if(i->press.active&&i->press.id==e->pointer_id)memset(&i->press,0,sizeof(i->press));
        return POCKET_UI_EVENT_CONTINUE;
    }
    PocketTextSnapshot s;
    if(!focused(i,&s))return POCKET_UI_EVENT_CONTINUE;
    if(e->type==POCKET_UI_EVENT_POINTER_DOWN){
        if(i->press.active)return POCKET_UI_EVENT_CONTINUE;
        i->press=(Press){.active=1,.id=e->pointer_id,.binding=(unsigned)(b-i->bindings),
                        .x=e->x,.y=e->y,.token=s.token};
    }else if(i->press.active&&i->press.id==e->pointer_id&&i->press.binding==(unsigned)(b-i->bindings)){
        Press *p=&i->press;
        if(e->type==POCKET_UI_EVENT_POINTER_MOVE){
            int64_t dx=(int64_t)e->x-p->x,dy=(int64_t)e->y-p->y;
            if(dx>12||dx< -12||dy>12||dy< -12){p->cancelled=1;p->repeating=0;}
        }else if(e->type==POCKET_UI_EVENT_POINTER_UP){p->released=1;p->repeating=0;}
        else if(e->type==POCKET_UI_EVENT_LONG_PRESS&&b->command==CMD_BACKSPACE&&!p->cancelled&&same_token(p->token,s.token)){
            p->repeating=1;p->next_repeat=e->timestamp_ms+REPEAT_MS;
            queue(i,CMD_BACKSPACE,0,p->token);
        }else if(e->type==POCKET_UI_EVENT_TAP&&!p->cancelled&&same_token(p->token,s.token)){
            unsigned value=b->value;
            if(b->command==CMD_CARET){
                int x=e->x-48;value=i->view_start+(unsigned)(x<0?0:(x+CELL_WIDTH/2)/CELL_WIDTH);
                if(value>s.graphemes)value=s.graphemes;
            }
            queue(i,b->command,value,p->token);
        }
    }
    return POCKET_UI_EVENT_CONTINUE;
}
static int bind(Impl *i,PocketComponentHandle c,unsigned command,unsigned value){
    if(i->binding_count==BINDINGS)return 0;
    Binding *b=&i->bindings[i->binding_count++];*b=(Binding){i,c,command,value};
    return pocket_ui_set_event_handler(i->config.tree,root_of(i,c),event,b)==POCKET_UI_OK;
}
static PocketComponentHandle button(Impl *i,PocketComponentHandle parent,int x,int y,int w,int h,
                                    uint64_t label,unsigned command,unsigned value){
    PocketComponentHandle c=make(i,POCKET_COMPONENT_BUTTON,parent,x,y,w,h,i->config.key_style,label);
    if(!bind(i,c,command,value))i->failed=1;
    if(command==CMD_BACKSPACE&&pocket_interaction_set_gestures(i->config.interaction,root_of(i,c),
       POCKET_GESTURE_TAP|POCKET_GESTURE_LONG_PRESS)!=POCKET_INTERACTION_OK)i->failed=1;
    return c;
}
static int digit_mode(const Impl *i){
    PocketKeyboardMode m=i->config.fields[i->active].mode;
    return m==POCKET_KEYBOARD_NUMBER||m==POCKET_KEYBOARD_PIN;
}
static int paint(Impl *i){
    PocketTextSnapshot s;char display[POCKET_KEYBOARD_MAX_CHARS+1];size_t length;
    if(pocket_text_snapshot(&i->sessions[i->active],&s)!=POCKET_TEXT_OK)return 0;
    if(s.focus_grapheme<i->view_start)i->view_start=s.focus_grapheme;
    if(s.focus_grapheme>=i->view_start+VIEW_CELLS)i->view_start=s.focus_grapheme-VIEW_CELLS+1;
    if(pocket_text_copy_display(&i->sessions[i->active],display,sizeof(display),&length)!=POCKET_TEXT_OK)return 0;
    for(unsigned n=0;n<VIEW_CELLS;n++){
        unsigned off=i->view_start+n;int show=off<length;
        if(!visible(i,i->glyphs[n],show)||(show&&!text(i,i->glyphs[n],POCKET_KEYBOARD_ASCII_BASE+(unsigned char)display[off])))return 0;
    }
    wipe(display,sizeof(display));
    unsigned lo=s.anchor_grapheme<s.focus_grapheme?s.anchor_grapheme:s.focus_grapheme;
    unsigned hi=s.anchor_grapheme>s.focus_grapheme?s.anchor_grapheme:s.focus_grapheme;
    if(lo<i->view_start)lo=i->view_start;
    if(hi>i->view_start+VIEW_CELLS)hi=i->view_start+VIEW_CELLS;
    if(!visible(i,i->selection,i->focused&&hi>lo)||!visible(i,i->caret,i->focused))return 0;
    if(hi>lo&&!layout(i,i->selection,16+(int)(lo-i->view_start)*CELL_WIDTH,8,(int)(hi-lo)*CELL_WIDTH,36))return 0;
    if(!layout(i,i->caret,16+(int)(s.focus_grapheme-i->view_start)*CELL_WIDTH,8,2,36))return 0;
    for(unsigned f=0;f<i->config.field_count;f++){
        if(pocket_text_copy_display(&i->sessions[f],display,sizeof(display),&length)!=POCKET_TEXT_OK)return 0;
        for(unsigned n=0;n<PREVIEW_CELLS;n++){
            int show=n<length;
            if(!visible(i,i->previews[f][n],show)||(show&&!text(i,i->previews[f][n],POCKET_KEYBOARD_ASCII_BASE+(unsigned char)display[n])))return 0;
        }
        wipe(display,sizeof(display));
        if(pocket_component_set_style_ref(i->config.components,i->fields[f],f==i->active?i->config.key_style:i->config.field_style)!=POCKET_COMPONENT_OK)return 0;
    }
    const char *letters[]={"qwertyuiop","asdfghjkl","zxcvbnm","0123456789"};
    const char *symbols[]={"1234567890","!@#$%^&*()","-_=+[]{}\\|",";:'\",./?`~<>"};
    const char *digits[]={"123","456","789","0"};
    const char **rows=digit_mode(i)?digits:i->mode?symbols:letters;
    unsigned key=0;
    for(unsigned row=0;row<4;row++)for(unsigned col=0;col<12;col++,key++){
        int show=col<strlen(rows[row]);unsigned c=show?(unsigned char)rows[row][col]:0;
        if(c>='a'&&c<='z'&&(i->shift!=i->caps))c-=32;
        for(unsigned b=0;b<i->binding_count;b++)if(i->bindings[b].component.slot==i->keys[key].slot&&i->bindings[b].component.generation==i->keys[key].generation)i->bindings[b].value=c;
        if(!visible(i,i->keys[key],show)||(show&&!text(i,i->keys[key],POCKET_KEYBOARD_ASCII_BASE+c)))return 0;
    }
    if(!visible(i,i->key_group,i->visible)||!text(i,i->hide_key,i->visible?LABEL_HIDE:LABEL_SHOW)||
       !text(i,i->mode_key,i->mode?LABEL_LETTERS:LABEL_SYMBOLS)||!disabled(i,i->mode_key,digit_mode(i))||
       !disabled(i,i->space_key,digit_mode(i))||!disabled(i,i->shift_key,digit_mode(i))||!disabled(i,i->caps_key,digit_mode(i)))return 0;
    if(pocket_component_set_style_ref(i->config.components,i->shift_key,i->shift?i->config.primary_style:i->config.key_style)!=POCKET_COMPONENT_OK||
       pocket_component_set_style_ref(i->config.components,i->caps_key,i->caps?i->config.primary_style:i->config.key_style)!=POCKET_COMPONENT_OK)return 0;
    uint64_t help=!i->focused?LABEL_PAUSED:i->edit_status==POCKET_TEXT_LIMIT?LABEL_LIMIT:
                   i->edit_status==POCKET_TEXT_POLICY?LABEL_POLICY:LABEL_HELP;
    if(!text(i,i->help,help))return 0;
    if(pocket_layout_run(i->config.layout,root_of(i,i->root),1024,i->config.height)!=POCKET_UI_OK)return 0;
    i->dirty=0;return 1;
}
static int close_overlay(Impl *i,PocketKeyboardResult result){
    lose_focus(i);i->result=result;
    int root_live=pocket_ui_handle_valid(root_of(i,i->root));
    if(!i->presented&&!root_live)return 1;
    PocketOverlaySnapshot overlay,top;
    int owns_entry=i->presented&&pocket_overlay_snapshot(i->config.overlays,i->config.overlay_id,&overlay)==POCKET_OVERLAY_OK&&
                   same_component(overlay.spec.root,i->root);
    /* Overlay IDs alone are not ownership. A replaced or covered keyboard
     * must not cancel another overlay's contacts or dismiss its reused ID. */
    if(owns_entry&&pocket_overlay_input_capture(i->config.overlays,&top)==POCKET_OVERLAY_OK&&
       same_component(top.spec.root,i->root))pocket_interaction_cancel_all(i->config.interaction,i->now);
    PocketInteractionSnapshot current;
    if(pocket_interaction_snapshot(i->config.interaction,&current)==POCKET_INTERACTION_OK){
        for(unsigned n=0;n<i->config.field_count;n++)if(same_ui(current.focused,root_of(i,i->fields[n])))
            (void)pocket_interaction_clear_focus(i->config.interaction);
    }
    if(owns_entry){
        PocketOverlayStatus status=pocket_overlay_dismiss(i->config.overlays,i->config.overlay_id);
        if(status!=POCKET_OVERLAY_OK&&!(status==POCKET_OVERLAY_STALE_COMPONENT&&!root_live))return 0;
    }else if(root_live){
        if(pocket_component_destroy(i->config.components,i->root)!=POCKET_COMPONENT_OK)return 0;
    }
    i->root=(PocketComponentHandle){0};i->presented=0;return 1;
}
static int execute(Impl *i,unsigned cmd,unsigned value,PocketTextToken token){
    PocketTextSnapshot s;if(!focused(i,&s)||!same_token(s.token,token))return 1;
    if(cmd==CMD_CONFIRM)return close_overlay(i,POCKET_KEYBOARD_CONFIRMED);
    if(cmd==CMD_CANCEL)return close_overlay(i,POCKET_KEYBOARD_CANCELLED);
    i->edit_status=POCKET_TEXT_OK;i->dirty=1;
    if(cmd==CMD_NEXT){
        for(unsigned n=1;n<=i->config.field_count;n++){
            unsigned next=(i->active+n)%i->config.field_count;
            if(i->config.fields[next].enabled)return pocket_interaction_focus(i->config.interaction,root_of(i,i->fields[next]))==POCKET_INTERACTION_OK;
        }
        return 1;
    }
    if(cmd==CMD_SHIFT||cmd==CMD_CAPS||cmd==CMD_MODE||cmd==CMD_HIDE){
        pocket_interaction_cancel_all(i->config.interaction,i->now);memset(&i->press,0,sizeof(i->press));
        if(cmd==CMD_SHIFT)i->shift=!i->shift;
        if(cmd==CMD_CAPS)i->caps=!i->caps;
        if(cmd==CMD_MODE)i->mode=!i->mode;
        if(cmd==CMD_HIDE)i->visible=!i->visible;
        return 1;
    }
    PocketTextAction action=cmd==CMD_INSERT?POCKET_TEXT_INSERT:cmd==CMD_BACKSPACE?POCKET_TEXT_BACKSPACE:
       cmd==CMD_LEFT?POCKET_TEXT_LEFT:cmd==CMD_RIGHT?POCKET_TEXT_RIGHT:cmd==CMD_HOME?POCKET_TEXT_HOME:
       cmd==CMD_END?POCKET_TEXT_END:cmd==CMD_ALL?POCKET_TEXT_SELECT_ALL:cmd==CMD_CLEAR?POCKET_TEXT_CLEAR:POCKET_TEXT_SELECT;
    char ch=(char)value;
    PocketTextEvent e={.version=POCKET_TEXT_API_VERSION,.token=token,.action=action,
                      .unit=POCKET_TEXT_GRAPHEME,.anchor=value,.focus=value};
    if((cmd==CMD_LEFT||cmd==CMD_RIGHT)&&i->shift){
        e.action=POCKET_TEXT_SELECT;e.anchor=s.anchor_grapheme;
        e.focus=cmd==CMD_LEFT?(s.focus_grapheme?s.focus_grapheme-1:0):
                 (s.focus_grapheme<s.graphemes?s.focus_grapheme+1:s.graphemes);
    }
    if(cmd==CMD_INSERT){
        if(value<32||value>126||(digit_mode(i)&&(value<'0'||value>'9'))){i->edit_status=POCKET_TEXT_POLICY;return 1;}
        e.text=&ch;e.text_bytes=1;
    }
    PocketTextEffect effect;
    i->edit_status=pocket_text_apply(&i->sessions[i->active],&e,&effect);ch=0;
    if(i->edit_status!=POCKET_TEXT_OK&&i->edit_status!=POCKET_TEXT_LIMIT&&i->edit_status!=POCKET_TEXT_POLICY&&
       i->edit_status!=POCKET_TEXT_READ_ONLY&&i->edit_status!=POCKET_TEXT_DISABLED)return 0;
    if(cmd==CMD_INSERT)i->shift=0;
    if(i->press.repeating&&pocket_text_snapshot(&i->sessions[i->active],&s)==POCKET_TEXT_OK)i->press.token=s.token;
    return 1;
}
int pocket_keyboard_open(PocketKeyboard *out,const PocketKeyboardConfig *config){
    if(!out||out->impl||!config||!config->tree||!config->layout||!config->components||!config->interaction||
       !config->overlays||!config->overlay_id||!config->field_count||config->field_count>4||
       (config->height!=600&&config->height!=800))return 0;
    Impl *i=calloc(1,sizeof(*i));if(!i)return 0;out->impl=i;i->config=*config;i->visible=1;i->dirty=1;
    unsigned enabled=0;
    for(unsigned f=0;f<config->field_count;f++){
        const PocketKeyboardField *field=&config->fields[f];
        if(!field->field_id||field->mode<POCKET_KEYBOARD_ASCII||field->mode>POCKET_KEYBOARD_PIN||!field->max_chars||field->max_chars>64||field->enabled>1||field->read_only>1)goto fail;
        for(unsigned prev=0;prev<f;prev++)if(config->fields[prev].field_id==field->field_id)goto fail;
        const char *initial=field->initial?field->initial:"";size_t n=0;
        for(;n<=64&&initial[n];n++)if((unsigned char)initial[n]<32||(unsigned char)initial[n]>126||
            ((field->mode==POCKET_KEYBOARD_NUMBER||field->mode==POCKET_KEYBOARD_PIN)&&(initial[n]<'0'||initial[n]>'9')))goto fail;
        if(n>64)goto fail;
        PocketTextConfig policy=pocket_text_config_default();policy.mode=POCKET_TEXT_ASCII;
        policy.max_bytes=policy.max_graphemes=field->max_chars;policy.enabled=field->enabled;policy.read_only=field->read_only;
        policy.sensitive=field->mode==POCKET_KEYBOARD_PASSWORD||field->mode==POCKET_KEYBOARD_PIN;
        if(pocket_text_init(&i->sessions[f],field->field_id,&policy,initial,n)!=POCKET_TEXT_OK)goto fail;
        i->config.fields[f].initial=NULL;enabled+=field->enabled;
    }
    if(!enabled)goto fail;
    i->root=make(i,POCKET_COMPONENT_MODAL,(PocketComponentHandle){0},0,0,1024,config->height,config->page_style,0);
    make(i,POCKET_COMPONENT_TEXT,i->root,32,16,440,36,config->text_style,LABEL_TITLE);
    i->help=make(i,POCKET_COMPONENT_TEXT,i->root,32,56,960,36,config->muted_style,LABEL_HELP);
    button(i,i->root,672,16,144,48,LABEL_CANCEL,CMD_CANCEL,0);
    button(i,i->root,832,16,160,48,LABEL_CONFIRM,CMD_CONFIRM,0);
    int width=(960-16*(int)(config->field_count-1))/(int)config->field_count;
    for(unsigned f=0;f<config->field_count;f++){
        i->fields[f]=make(i,POCKET_COMPONENT_TEXT_FIELD,i->root,32+(int)f*(width+16),104,width,84,config->field_style,0);
        if(!bind(i,i->fields[f],CMD_FIELD,f)||!disabled(i,i->fields[f],!config->fields[f].enabled))goto fail;
        make(i,POCKET_COMPONENT_TEXT,i->fields[f],12,4,width-24,36,config->muted_style,config->fields[f].label_ref);
        for(unsigned n=0;n<PREVIEW_CELLS;n++)i->previews[f][n]=make(i,POCKET_COMPONENT_TEXT,i->fields[f],12+(int)n*CELL_WIDTH,40,CELL_WIDTH,36,config->text_style,0);
    }
    i->band=button(i,i->root,32,208,960,56,0,CMD_CARET,0);
    if(pocket_component_set_style_ref(config->components,i->band,config->field_style)!=POCKET_COMPONENT_OK)goto fail;
    i->selection=make(i,POCKET_COMPONENT_VIEW,i->band,16,8,1,36,config->key_style,0);
    for(unsigned n=0;n<VIEW_CELLS;n++)i->glyphs[n]=make(i,POCKET_COMPONENT_TEXT,i->band,16+(int)n*CELL_WIDTH,8,CELL_WIDTH,36,config->text_style,0);
    i->caret=make(i,POCKET_COMPONENT_VIEW,i->band,16,8,2,36,config->primary_style,0);
    const unsigned commands[]={CMD_HOME,CMD_LEFT,CMD_RIGHT,CMD_END,CMD_ALL,CMD_CLEAR,CMD_SHIFT,CMD_CAPS};
    for(unsigned n=0;n<8;n++){
        PocketComponentHandle c=button(i,i->root,32+(int)n*120,276,112,48,LABEL_HOME+n,commands[n],0);
        if(n==6)i->shift_key=c;
        if(n==7)i->caps_key=c;
    }
    i->key_group=make(i,POCKET_COMPONENT_VIEW,i->root,0,(int)config->height-256,1024,240,config->page_style,0);
    for(unsigned n=0;n<KEY_CELLS;n++)i->keys[n]=button(i,i->key_group,32+(int)(n%12)*62,(int)(n/12)*60,54,52,0,CMD_INSERT,0);
    i->mode_key=button(i,i->key_group,816,0,176,52,LABEL_SYMBOLS,CMD_MODE,0);
    button(i,i->key_group,816,60,176,52,LABEL_BACKSPACE,CMD_BACKSPACE,0);
    i->space_key=button(i,i->key_group,816,120,176,52,LABEL_SPACE,CMD_INSERT,32);
    button(i,i->key_group,816,180,176,52,LABEL_NEXT,CMD_NEXT,0);
    /* Kept outside the hidden key group so the keyboard can always reopen. */
    i->hide_key=button(i,i->root,496,16,160,48,LABEL_HIDE,CMD_HIDE,0);
    if(i->failed)goto fail;
    if(pocket_overlay_present(config->overlays,&(PocketOverlaySpec){.id=config->overlay_id,.owner_route=config->owner_route,
       .kind=POCKET_OVERLAY_KEYBOARD,.root=i->root,.focus_token=((uint64_t)i->root.generation<<32)|i->root.slot,
       .owns_root=1,.captures_input=1,.captures_focus=1,.dismiss_on_back=1})!=POCKET_OVERLAY_OK)goto fail;
    i->presented=1;
    while(!config->fields[i->active].enabled)i->active++;
    if(pocket_layout_run(config->layout,root_of(i,i->root),1024,config->height)!=POCKET_UI_OK||
       pocket_interaction_focus(config->interaction,root_of(i,i->fields[i->active]))!=POCKET_INTERACTION_OK||!paint(i))goto fail;
    return 1;
fail:pocket_keyboard_dispose(out);return 0;
}
int pocket_keyboard_step(PocketKeyboard *out,uint64_t ms){
    Impl *i=out?out->impl:NULL;if(!i||i->failed||ms<i->now)return 0;i->now=ms;
    if(i->result!=POCKET_KEYBOARD_EDITING)return 1;
    PocketOverlaySnapshot overlay;
    if(pocket_overlay_snapshot(i->config.overlays,i->config.overlay_id,&overlay)!=POCKET_OVERLAY_OK||
       !same_component(overlay.spec.root,i->root)||!pocket_ui_handle_valid(root_of(i,i->root)))return close_overlay(i,POCKET_KEYBOARD_CANCELLED);
    PocketTextSnapshot s;
    if(!focused(i,&s))lose_focus(i);
    if(i->pending){
        unsigned cmd=i->pending_command,value=i->pending_value;PocketTextToken token=i->pending_token;i->pending=0;
        if(!execute(i,cmd,value,token))return 0;
    }
    if(i->result!=POCKET_KEYBOARD_EDITING)return 1;
    if(i->press.released)memset(&i->press,0,sizeof(i->press));
    if(i->press.repeating&&ms>=i->press.next_repeat){
        if(!focused(i,&s)||!same_token(s.token,i->press.token))memset(&i->press,0,sizeof(i->press));
        else{if(!execute(i,CMD_BACKSPACE,0,i->press.token))return 0;i->press.next_repeat=ms+REPEAT_MS;}
    }
    return !i->dirty||paint(i);
}
int pocket_keyboard_snapshot(const PocketKeyboard *out,PocketKeyboardSnapshot *s){
    const Impl *i=out?out->impl:NULL;if(!i||!s)return 0;
    PocketKeyboardMode m=i->config.fields[i->active].mode;
    *s=(PocketKeyboardSnapshot){i->result,i->active,i->config.field_count,(uint8_t)i->visible,
       (uint8_t)(m==POCKET_KEYBOARD_PASSWORD||m==POCKET_KEYBOARD_PIN),i->edit_status};return 1;
}
PocketUiHandle pocket_keyboard_root(const PocketKeyboard *out){
    Impl *i=out?out->impl:NULL;return i?root_of(i,i->root):(PocketUiHandle){0};
}
PocketTextStatus pocket_keyboard_copy_result(const PocketKeyboard *out,uint32_t field,char *text_out,size_t capacity,size_t *bytes){
    const Impl *i=out?out->impl:NULL;
    if(!i||i->result!=POCKET_KEYBOARD_CONFIRMED||field>=i->config.field_count)return POCKET_TEXT_INVALID_ARGUMENT;
    return pocket_text_copy_committed(&i->sessions[field],text_out,capacity,bytes);
}
void pocket_keyboard_dispose(PocketKeyboard *out){
    Impl *i=out?out->impl:NULL;if(!i)return;
    (void)close_overlay(i,POCKET_KEYBOARD_CANCELLED);
    for(unsigned f=0;f<POCKET_KEYBOARD_MAX_FIELDS;f++)pocket_text_dispose(&i->sessions[f]);
    wipe(i,sizeof(*i));free(i);out->impl=NULL;
}
