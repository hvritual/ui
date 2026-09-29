#include "style.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STYLE_DEFAULT_THEMES 8U
#define STYLE_DEFAULT_TOKENS 256U
#define STYLE_DEFAULT_RULES 512U

typedef struct {
    uint32_t id;
    PocketStyle base;
    uint32_t token_start;
    uint32_t token_count;
    int used;
} ThemeRecord;

typedef struct {
    PocketStyleRule rule;
    uint32_t sequence;
    int used;
} RuleRecord;

typedef struct {
    ThemeRecord *themes;
    PocketThemeToken *tokens;
    RuleRecord *rules;
    uint32_t theme_capacity, token_capacity, rule_capacity;
    uint32_t theme_count, token_count, rule_count;
    uint32_t active_theme;
    uint64_t epoch;
    uint32_t next_sequence;
} StyleImpl;

static StyleImpl *si(PocketStyleRuntime *runtime){return runtime?(StyleImpl *)runtime->impl:NULL;}
static const StyleImpl *csi(const PocketStyleRuntime *runtime){return runtime?(const StyleImpl *)runtime->impl:NULL;}

PocketStyleAtom pocket_style_literal(int64_t value){
    PocketStyleAtom a={POCKET_STYLE_LITERAL,0,value};return a;
}
PocketStyleAtom pocket_style_token(uint32_t token){
    PocketStyleAtom a={POCKET_STYLE_TOKEN,token,0};return a;
}
static int atom_valid(PocketStyleAtom a){
    return (a.source==POCKET_STYLE_LITERAL && a.token==0U) ||
           (a.source==POCKET_STYLE_TOKEN && a.token!=0U && a.value==0);
}
static int field_value_valid(unsigned field,int64_t value){
    switch(field){
        case POCKET_STYLE_BACKGROUND:
        case POCKET_STYLE_FOREGROUND:
        case POCKET_STYLE_BORDER_COLOR:
            return value>=0 && (uint64_t)value<=0xffffffffULL;
        case POCKET_STYLE_BORDER_WIDTH:
        case POCKET_STYLE_RADIUS:
        case POCKET_STYLE_FONT_ID:
        case POCKET_STYLE_FONT_SIZE:
        case POCKET_STYLE_SPACING:
            return value>=0 && value<=INT32_MAX;
        case POCKET_STYLE_OPACITY:
            return value>=0 && value<=256;
        case POCKET_STYLE_TRANSLATE_X:
        case POCKET_STYLE_TRANSLATE_Y:
            return value>=INT32_MIN && value<=INT32_MAX;
        default:
            return 0;
    }
}
static int style_valid(const PocketStyle *s){
    if(!s || (s->set_mask & ~POCKET_STYLE_ALL))return 0;
    for(unsigned i=0;i<POCKET_STYLE_FIELD_COUNT;i++)if(s->set_mask & POCKET_STYLE_BIT(i)){
        if(!atom_valid(s->fields[i]))return 0;
        if(s->fields[i].source==POCKET_STYLE_LITERAL && !field_value_valid(i,s->fields[i].value))
            return 0;
    }
    return 1;
}
PocketStyleStatus pocket_style_runtime_init(PocketStyleRuntime *runtime,
                                             const PocketStyleRuntimeConfig *config){
    if(!runtime || runtime->impl)return POCKET_STYLE_INVALID_ARGUMENT;
    uint32_t tc=config&&config->theme_capacity?config->theme_capacity:STYLE_DEFAULT_THEMES;
    uint32_t kc=config&&config->token_capacity?config->token_capacity:STYLE_DEFAULT_TOKENS;
    uint32_t rc=config&&config->rule_capacity?config->rule_capacity:STYLE_DEFAULT_RULES;
    if(!tc||!kc||!rc||tc>256U||kc>65535U||rc>512U)return POCKET_STYLE_INVALID_ARGUMENT;
    StyleImpl *impl=calloc(1,sizeof(*impl));if(!impl)return POCKET_STYLE_RESOURCE_EXHAUSTED;
    impl->themes=calloc(tc,sizeof(*impl->themes));impl->tokens=calloc(kc,sizeof(*impl->tokens));
    impl->rules=calloc(rc,sizeof(*impl->rules));
    if(!impl->themes||!impl->tokens||!impl->rules){
        free(impl->rules);free(impl->tokens);free(impl->themes);free(impl);
        return POCKET_STYLE_RESOURCE_EXHAUSTED;
    }
    impl->theme_capacity=tc;impl->token_capacity=kc;impl->rule_capacity=rc;impl->next_sequence=1;
    runtime->impl=impl;return POCKET_STYLE_OK;
}
void pocket_style_runtime_dispose(PocketStyleRuntime *runtime){
    StyleImpl *impl=si(runtime);if(!impl)return;
    free(impl->rules);free(impl->tokens);free(impl->themes);free(impl);runtime->impl=NULL;
}
static ThemeRecord *theme_by_id(StyleImpl *impl,uint32_t id){
    if(!impl||!id)return NULL;
    for(uint32_t i=0;i<impl->theme_capacity;i++)if(impl->themes[i].used&&impl->themes[i].id==id)return &impl->themes[i];
    return NULL;
}
static const ThemeRecord *theme_by_id_const(const StyleImpl *impl,uint32_t id){
    if(!impl||!id)return NULL;
    for(uint32_t i=0;i<impl->theme_capacity;i++)if(impl->themes[i].used&&impl->themes[i].id==id)return &impl->themes[i];
    return NULL;
}
PocketStyleStatus pocket_style_add_theme(PocketStyleRuntime *runtime,
                                         const PocketThemeDefinition *theme){
    StyleImpl *impl=si(runtime);
    if(!impl||!theme||!theme->id||!style_valid(&theme->base)||
       (theme->token_count && !theme->tokens))return POCKET_STYLE_INVALID_ARGUMENT;
    if(theme_by_id(impl,theme->id))return POCKET_STYLE_INVALID_ARGUMENT;
    if(impl->theme_count>=impl->theme_capacity ||
       theme->token_count>impl->token_capacity-impl->token_count)
        return POCKET_STYLE_RESOURCE_EXHAUSTED;
    for(uint32_t i=0;i<theme->token_count;i++){
        if(!theme->tokens[i].id)return POCKET_STYLE_INVALID_ARGUMENT;
        for(uint32_t j=0;j<i;j++)if(theme->tokens[j].id==theme->tokens[i].id)return POCKET_STYLE_INVALID_ARGUMENT;
    }
    ThemeRecord *record=NULL;
    for(uint32_t i=0;i<impl->theme_capacity;i++)if(!impl->themes[i].used){record=&impl->themes[i];break;}
    if(!record)return POCKET_STYLE_RESOURCE_EXHAUSTED;
    record->used=1;record->id=theme->id;record->base=theme->base;
    record->token_start=impl->token_count;record->token_count=theme->token_count;
    if(theme->token_count)
        memcpy(&impl->tokens[impl->token_count],theme->tokens,(size_t)theme->token_count*sizeof(*theme->tokens));
    impl->token_count+=theme->token_count;impl->theme_count++;
    if(!impl->active_theme){impl->active_theme=theme->id;impl->epoch=1;}
    return POCKET_STYLE_OK;
}
PocketStyleStatus pocket_style_add_rule(PocketStyleRuntime *runtime,const PocketStyleRule *rule){
    StyleImpl *impl=si(runtime);
    if(!impl||!rule||!rule->style_ref||(rule->required_states&~POCKET_STATE_ALL)||
       !style_valid(&rule->style))return POCKET_STYLE_INVALID_ARGUMENT;
    if(impl->rule_count>=impl->rule_capacity)return POCKET_STYLE_RESOURCE_EXHAUSTED;
    for(uint32_t i=0;i<impl->rule_capacity;i++)if(!impl->rules[i].used){
        impl->rules[i].used=1;
        impl->rules[i].rule=*rule;
        impl->rules[i].sequence=impl->next_sequence++;
        if(!impl->next_sequence) impl->next_sequence=1;
        impl->rule_count++;
        return POCKET_STYLE_OK;
    }
    return POCKET_STYLE_RESOURCE_EXHAUSTED;
}
PocketStyleStatus pocket_style_set_theme(PocketStyleRuntime *runtime,uint32_t theme_id){
    StyleImpl *impl=si(runtime);if(!impl||!theme_id)return POCKET_STYLE_INVALID_ARGUMENT;
    if(!theme_by_id(impl,theme_id))return POCKET_STYLE_NOT_FOUND;
    if(impl->active_theme!=theme_id){impl->active_theme=theme_id;impl->epoch++;if(!impl->epoch)impl->epoch=1;}
    return POCKET_STYLE_OK;
}
uint32_t pocket_style_theme(const PocketStyleRuntime *runtime){
    const StyleImpl *impl=csi(runtime);return impl?impl->active_theme:0;
}
uint64_t pocket_style_theme_epoch(const PocketStyleRuntime *runtime){
    const StyleImpl *impl=csi(runtime);return impl?impl->epoch:0;
}
static int token_value(const StyleImpl *impl,const ThemeRecord *theme,uint32_t id,int64_t *out){
    for(uint32_t i=0;i<theme->token_count;i++){
        const PocketThemeToken *t=&impl->tokens[theme->token_start+i];
        if(t->id==id){*out=t->value;return 1;}
    }
    return 0;
}
static PocketStyleStatus apply_style(const StyleImpl *impl,const ThemeRecord *theme,
                                     const PocketStyle *style,PocketResolvedStyle *out){
    for(unsigned i=0;i<POCKET_STYLE_FIELD_COUNT;i++)if(style->set_mask&POCKET_STYLE_BIT(i)){
        PocketStyleAtom a=style->fields[i];int64_t value=a.value;
        if(a.source==POCKET_STYLE_TOKEN && !token_value(impl,theme,a.token,&value))
            return POCKET_STYLE_TOKEN_MISSING;
        if(!field_value_valid(i,value)) return POCKET_STYLE_INVALID_ARGUMENT;
        out->fields[i]=value;out->set_mask|=POCKET_STYLE_BIT(i);
    }
    return POCKET_STYLE_OK;
}
static int rule_before(const RuleRecord *a,const RuleRecord *b){
    if(a->rule.priority!=b->rule.priority)return a->rule.priority<b->rule.priority;
    return a->sequence<b->sequence;
}
PocketStyleStatus pocket_style_resolve(const PocketStyleRuntime *runtime,
                                       uint64_t style_ref,uint32_t states,
                                       const PocketResolvedStyle *parent,
                                       PocketResolvedStyle *out){
    const StyleImpl *impl=csi(runtime);
    if(!impl||!out||(states&~POCKET_STATE_ALL)||!impl->active_theme)return POCKET_STYLE_INVALID_ARGUMENT;
    const ThemeRecord *theme=theme_by_id_const(impl,impl->active_theme);
    if(!theme)return POCKET_STYLE_NOT_FOUND;
    memset(out,0,sizeof(*out));
    PocketStyleStatus status=apply_style(impl,theme,&theme->base,out);if(status!=POCKET_STYLE_OK)return status;
    if(parent)for(unsigned i=0;i<POCKET_STYLE_FIELD_COUNT;i++)
        if((POCKET_STYLE_INHERITED&POCKET_STYLE_BIT(i))&&(parent->set_mask&POCKET_STYLE_BIT(i))){
            out->fields[i]=parent->fields[i];out->set_mask|=POCKET_STYLE_BIT(i);
        }
    const RuleRecord *ordered[512];
    uint32_t count=0;
    for(uint32_t i=0;i<impl->rule_capacity;i++){
        const RuleRecord *r=&impl->rules[i];
        if(!r->used||r->rule.style_ref!=style_ref)continue;
        if((states&r->rule.required_states)!=r->rule.required_states)continue;
        uint32_t pos=count;
        while(pos>0&&rule_before(r,ordered[pos-1])){ordered[pos]=ordered[pos-1];pos--;}
        ordered[pos]=r;count++;
    }
    for(uint32_t i=0;i<count;i++){
        status=apply_style(impl,theme,&ordered[i]->rule.style,out);
        if(status!=POCKET_STYLE_OK)return status;
    }
    return POCKET_STYLE_OK;
}
