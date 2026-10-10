#include "session.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    PocketTextSession *text;
    const void *dictionary;size_t dictionary_bytes;
    PocketPinyin provider;PocketInputLocale locale;
    PocketTextToken token;PocketPinyinResult candidates;
    PocketPinyinStatus status;
    char raw[POCKET_PINYIN_MAX_INPUT+1],prefix[POCKET_PINYIN_MAX_TEXT];
    size_t raw_bytes,prefix_bytes;
    uint64_t request;
    int pending;
} Session;
static void wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static int equal(PocketTextToken a,PocketTextToken b){return a.field_id==b.field_id&&a.session_id==b.session_id&&a.focus_generation==b.focus_generation&&a.engine_generation==b.engine_generation&&a.revision==b.revision;}
static PocketTextStatus current(Session *s,PocketTextToken token,PocketTextSnapshot *out){
    if(!s)return POCKET_TEXT_INVALID_ARGUMENT;
    PocketTextStatus status=pocket_text_snapshot(s->text,out);if(status!=POCKET_TEXT_OK)return status;
    if(!out->active)return POCKET_TEXT_NOT_FOCUSED;
    if(!equal(token,out->token)||!equal(out->token,s->token))return POCKET_TEXT_STALE_EVENT;
    if(out->sensitive)return POCKET_TEXT_POLICY;
    if(out->read_only)return POCKET_TEXT_READ_ONLY;
    return POCKET_TEXT_OK;
}
static PocketTextStatus apply(Session *s,PocketTextAction action,const char *text,size_t bytes,PocketTextEffect *effect){
    PocketTextSnapshot state;if(pocket_text_snapshot(s->text,&state)!=POCKET_TEXT_OK)return POCKET_TEXT_INVALID_ARGUMENT;
    PocketTextEvent event={.version=POCKET_TEXT_API_VERSION,.token=state.token,.action=action,.text=text,.text_bytes=bytes,.unit=POCKET_TEXT_GRAPHEME};
    PocketTextStatus status=pocket_text_apply(s->text,&event,effect);
    if(status==POCKET_TEXT_OK){(void)pocket_text_snapshot(s->text,&state);s->token=state.token;}return status;
}
static void clear(Session *s){
    pocket_pinyin_cancel(&s->provider);wipe(s->raw,sizeof(s->raw));wipe(s->prefix,sizeof(s->prefix));wipe(&s->candidates,sizeof(s->candidates));
    s->raw_bytes=s->prefix_bytes=0;s->request=0;s->pending=0;
}
static PocketTextStatus query(Session *s,uint32_t page,uint64_t now){
    wipe(&s->candidates,sizeof(s->candidates));s->pending=1;
    PocketPinyinStatus status=pocket_pinyin_request(&s->provider,s->token,s->raw,s->raw_bytes,page,now,&s->request);
    s->status=status;
    if(status!=POCKET_PINYIN_OK){s->pending=0;return POCKET_TEXT_POLICY;}
    return POCKET_TEXT_OK;
}
static PocketTextStatus preedit(Session *s){
    char buffer[POCKET_PINYIN_MAX_TEXT+POCKET_PINYIN_MAX_INPUT+1];
    memcpy(buffer,s->prefix,s->prefix_bytes);memcpy(buffer+s->prefix_bytes,s->raw,s->raw_bytes);
    PocketTextEffect effect;PocketTextStatus status=apply(s,POCKET_TEXT_COMPOSITION_UPDATE,buffer,s->prefix_bytes+s->raw_bytes,&effect);
    wipe(buffer,sizeof(buffer));return status;
}
int pocket_ime_open(PocketImeSession *out,PocketTextSession *text,const void *dict,size_t bytes){
    PocketTextSnapshot state;
    if(!out||out->impl||!text||pocket_text_snapshot(text,&state)!=POCKET_TEXT_OK||state.sensitive||state.read_only||!state.enabled||state.composing||!!dict!=!!bytes)return 0;
    Session *s=calloc(1,sizeof(*s));if(!s)return 0;s->text=text;s->dictionary=dict;s->dictionary_bytes=bytes;s->token=state.token;out->impl=s;return 1;
}
PocketTextStatus pocket_ime_locale(PocketImeSession *out,PocketTextToken token,PocketInputLocale locale,uint64_t now){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    if(locale!=POCKET_INPUT_EN_US&&locale!=POCKET_INPUT_ZH_CN)return POCKET_TEXT_INVALID_ARGUMENT;
    if(locale==s->locale)return POCKET_TEXT_OK;
    if(locale==POCKET_INPUT_ZH_CN&&!s->provider.impl){
        s->status=pocket_pinyin_open(&s->provider,s->dictionary,s->dictionary_bytes,now);
        if(s->status!=POCKET_PINYIN_OK)return POCKET_TEXT_POLICY;
    }
    r=pocket_text_engine_reset(s->text);if(r!=POCKET_TEXT_OK)return r;
    clear(s);(void)pocket_text_snapshot(s->text,&state);s->token=state.token;s->locale=locale;
    if(locale==POCKET_INPUT_EN_US)pocket_pinyin_close(&s->provider);
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_ime_key(PocketImeSession *out,PocketTextToken token,char ascii,uint64_t now){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    if(ascii<32||ascii>126)return POCKET_TEXT_POLICY;
    PocketTextEffect effect;
    if(s->locale==POCKET_INPUT_EN_US)return apply(s,POCKET_TEXT_INSERT,&ascii,1,&effect);
    if(!((ascii>='a'&&ascii<='z')||ascii=='\''))return POCKET_TEXT_POLICY;
    if(s->raw_bytes==POCKET_PINYIN_MAX_INPUT)return POCKET_TEXT_LIMIT;
    if(ascii=='\''&&(!s->raw_bytes||s->raw[s->raw_bytes-1]=='\''))return POCKET_TEXT_POLICY;
    if(!state.composing){r=apply(s,POCKET_TEXT_COMPOSITION_BEGIN,NULL,0,&effect);if(r!=POCKET_TEXT_OK)return r;}
    s->raw[s->raw_bytes++]=ascii;s->raw[s->raw_bytes]=0;r=preedit(s);if(r!=POCKET_TEXT_OK)return r;
    /* A trailing syllable separator is valid preedit but not a query yet. */
    if(ascii=='\''){pocket_pinyin_cancel(&s->provider);wipe(&s->candidates,sizeof(s->candidates));s->pending=0;return POCKET_TEXT_OK;}
    return query(s,0,now);
}
PocketTextStatus pocket_ime_cancel(PocketImeSession *out,PocketTextToken token){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    if(state.composing){PocketTextEffect e;r=apply(s,POCKET_TEXT_COMPOSITION_CANCEL,NULL,0,&e);if(r!=POCKET_TEXT_OK)return r;}
    clear(s);return POCKET_TEXT_OK;
}
PocketTextStatus pocket_ime_backspace(PocketImeSession *out,PocketTextToken token,uint64_t now){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    PocketTextEffect effect;
    if(!state.composing)return apply(s,POCKET_TEXT_BACKSPACE,NULL,0,&effect);
    if(s->raw_bytes){s->raw[--s->raw_bytes]=0;}
    else return pocket_ime_cancel(out,token);
    if(!s->raw_bytes&&!s->prefix_bytes)return pocket_ime_cancel(out,token);
    r=preedit(s);if(r!=POCKET_TEXT_OK)return r;
    if(!s->raw_bytes||s->raw[s->raw_bytes-1]=='\''){pocket_pinyin_cancel(&s->provider);wipe(&s->candidates,sizeof(s->candidates));s->pending=0;return POCKET_TEXT_OK;}
    return query(s,0,now);
}
PocketTextStatus pocket_ime_page(PocketImeSession *out,PocketTextToken token,uint32_t page,uint64_t now){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    if(!state.composing||s->pending||!s->raw_bytes||!s->candidates.total||page>(s->candidates.total-1)/POCKET_PINYIN_PAGE_SIZE)return POCKET_TEXT_INVALID_ARGUMENT;
    return query(s,page,now);
}
PocketTextStatus pocket_ime_choose(PocketImeSession *out,PocketTextToken token,uint64_t request,uint32_t index,uint64_t now){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;
    if(!state.composing||s->pending||request!=s->request||request!=s->candidates.request||!equal(state.token,s->candidates.token)||index>=s->candidates.count)return POCKET_TEXT_STALE_EVENT;
    const PocketPinyinCandidate *c=&s->candidates.candidates[index];size_t length=strlen(c->text),used=c->consumed_bytes;
    if(!used||used>s->raw_bytes||s->prefix_bytes+length>=sizeof(s->prefix))return POCKET_TEXT_LIMIT;
    char composed[POCKET_PINYIN_MAX_TEXT];memcpy(composed,s->prefix,s->prefix_bytes);memcpy(composed+s->prefix_bytes,c->text,length);
    size_t total=s->prefix_bytes+length;composed[total]=0;
    if(used==s->raw_bytes){PocketTextEffect effect;r=apply(s,POCKET_TEXT_COMPOSITION_COMMIT,composed,total,&effect);if(r==POCKET_TEXT_OK)clear(s);}
    else {
        memcpy(s->prefix,composed,total+1);s->prefix_bytes=total;
        memmove(s->raw,s->raw+used,s->raw_bytes-used);s->raw_bytes-=used;s->raw[s->raw_bytes]=0;
        if(s->raw[0]=='\''){memmove(s->raw,s->raw+1,s->raw_bytes);s->raw_bytes--;}
        r=preedit(s);if(r==POCKET_TEXT_OK)r=query(s,0,now);
    }
    wipe(composed,sizeof(composed));return r;
}
PocketTextStatus pocket_ime_enter(PocketImeSession *out,PocketTextToken token,uint64_t now,PocketTextEffect *effect){
    Session *s=out?out->impl:NULL;PocketTextSnapshot state;if(effect)*effect=POCKET_TEXT_EFFECT_NONE;
    PocketTextStatus r=current(s,token,&state);if(r!=POCKET_TEXT_OK)return r;if(!effect)return POCKET_TEXT_INVALID_ARGUMENT;
    if(state.composing){
        if(!s->raw_bytes&&s->prefix_bytes){r=apply(s,POCKET_TEXT_COMPOSITION_COMMIT,s->prefix,s->prefix_bytes,effect);if(r==POCKET_TEXT_OK)clear(s);return r;}
        if(s->pending||!s->candidates.count){*effect=POCKET_TEXT_EFFECT_ACCEPT_CANDIDATE;return POCKET_TEXT_OK;}
        r=pocket_ime_choose(out,token,s->request,0,now);if(r==POCKET_TEXT_OK){(void)pocket_text_snapshot(s->text,&state);*effect=state.composing?POCKET_TEXT_EFFECT_ACCEPT_CANDIDATE:POCKET_TEXT_EFFECT_COMMITTED;}return r;
    }
    return apply(s,POCKET_TEXT_ENTER,NULL,0,effect);
}
PocketPinyinStatus pocket_ime_step(PocketImeSession *out,uint64_t now){
    Session *s=out?out->impl:NULL;if(!s)return POCKET_PINYIN_INVALID;
    PocketTextSnapshot state;if(pocket_text_snapshot(s->text,&state)!=POCKET_TEXT_OK)return POCKET_PINYIN_INVALID;
    if(!state.active||!equal(state.token,s->token)){
        /* F6/owner already changed the session; never write an old result. */
        clear(s);s->token=state.token;return POCKET_PINYIN_PENDING;
    }
    if(!s->provider.impl)return POCKET_PINYIN_OK;
    PocketPinyinResult result={0};PocketPinyinStatus status=pocket_pinyin_poll(&s->provider,now,&result);s->status=status;
    if(status==POCKET_PINYIN_OK&&s->pending&&result.request==s->request&&equal(result.token,state.token)){
        s->candidates=result;s->pending=0;
    }else if(status!=POCKET_PINYIN_OK&&status!=POCKET_PINYIN_PENDING){wipe(&s->candidates,sizeof(s->candidates));s->pending=0;}
    wipe(&result,sizeof(result));return status;
}
int pocket_ime_snapshot(const PocketImeSession *out,PocketImeSnapshot *snapshot){
    const Session *s=out?out->impl:NULL;PocketTextSnapshot text;if(!s||!snapshot||pocket_text_snapshot(s->text,&text)!=POCKET_TEXT_OK)return 0;
    int stage=0,system_errno=0;unsigned storage=0,verified=0;
    pocket_pinyin_diagnostics(&s->provider,&stage,&system_errno,&storage,&verified);
    *snapshot=(PocketImeSnapshot){s->locale,s->status,s->candidates.count,s->candidates.page,s->candidates.total,s->request,
        text.composing,(uint8_t)s->pending,stage,system_errno,storage,verified};return 1;
}
int pocket_ime_candidates(const PocketImeSession *out,PocketPinyinResult *result){
    const Session *s=out?out->impl:NULL;PocketTextSnapshot state;
    if(result)memset(result,0,sizeof(*result));
    if(!s||!result||s->pending||pocket_text_snapshot(s->text,&state)!=POCKET_TEXT_OK||!state.active||!equal(state.token,s->candidates.token))return 0;
    *result=s->candidates;return 1;
}
void pocket_ime_close(PocketImeSession *out){
    Session *s=out?out->impl:NULL;if(!s)return;
    PocketTextSnapshot state;if(pocket_text_snapshot(s->text,&state)==POCKET_TEXT_OK&&state.active&&equal(state.token,s->token)){
        if(state.composing){PocketTextEffect effect;(void)apply(s,POCKET_TEXT_COMPOSITION_CANCEL,NULL,0,&effect);}
    }
    pocket_pinyin_close(&s->provider);wipe(s,sizeof(*s));free(s);out->impl=NULL;
}
