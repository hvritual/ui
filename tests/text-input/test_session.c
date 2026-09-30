#include "hosts/linux/text-input/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"TEXT_FAIL line=%d\n",__LINE__); return 0; } } while (0)
static unsigned groups;
static PocketTextSnapshot snapshot(const PocketTextSession *s) {
    PocketTextSnapshot out;
    if (pocket_text_snapshot(s,&out)!=POCKET_TEXT_OK) abort();
    return out;
}
static PocketTextEvent event(const PocketTextSession *s, PocketTextAction action, const char *text) {
    PocketTextEvent e = {0}; e.version=1; e.token=snapshot(s).token; e.action=action;
    e.text=text; e.text_bytes=text?strlen(text):0; e.unit=POCKET_TEXT_GRAPHEME; return e;
}
static PocketTextStatus send(PocketTextSession *s, PocketTextAction a, const char *text) {
    PocketTextEvent e=event(s,a,text); PocketTextEffect effect;
    return pocket_text_apply(s,&e,&effect);
}
static int value(const PocketTextSession *s, const char *want) {
    char out[POCKET_TEXT_MAX_BYTES+1]; size_t bytes=0;
    return pocket_text_copy_committed(s,out,sizeof(out),&bytes)==POCKET_TEXT_OK &&
           bytes==strlen(want) && !memcmp(out,want,bytes+1);
}
static int begin(PocketTextSession *s, const char *text, PocketTextConfig c) {
    return pocket_text_init(s,7,&c,text,strlen(text))==POCKET_TEXT_OK && pocket_text_focus(s)==POCKET_TEXT_OK;
}
static int unchanged(PocketTextSession *s, PocketTextEvent *e, PocketTextStatus expected) {
    PocketTextSnapshot before=snapshot(s); PocketTextEffect effect;
    char old[4097],now[4097];size_t oldn,n;
    CHECK(pocket_text_copy_committed(s,old,sizeof(old),&oldn)==POCKET_TEXT_OK);
    CHECK(pocket_text_apply(s,e,&effect)==expected && effect==POCKET_TEXT_EFFECT_NONE);
    PocketTextSnapshot after=snapshot(s);
    CHECK(!memcmp(&before,&after,sizeof(before)));
    CHECK(pocket_text_copy_committed(s,now,sizeof(now),&n)==POCKET_TEXT_OK && n==oldn && !memcmp(now,old,n+1));
    return 1;
}
static int editing(void) {
    PocketTextSession s={0};CHECK(begin(&s,"Ae\xcc\x81😀🇨🇳Z",pocket_text_config_default()));
    CHECK(snapshot(&s).graphemes==5 && snapshot(&s).utf16_units==10);
    CHECK(send(&s,POCKET_TEXT_BACKSPACE,NULL)==POCKET_TEXT_OK && value(&s,"Ae\xcc\x81😀🇨🇳"));
    CHECK(send(&s,POCKET_TEXT_BACKSPACE,NULL)==POCKET_TEXT_OK && value(&s,"Ae\xcc\x81😀"));
    CHECK(send(&s,POCKET_TEXT_LEFT,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_DELETE,NULL)==POCKET_TEXT_OK && value(&s,"Ae\xcc\x81"));
    CHECK(send(&s,POCKET_TEXT_BACKSPACE,NULL)==POCKET_TEXT_OK && value(&s,"A"));
    CHECK(send(&s,POCKET_TEXT_HOME,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_INSERT,"中文") ==POCKET_TEXT_OK && value(&s,"中文A"));
    PocketTextEvent e=event(&s,POCKET_TEXT_SELECT,NULL);e.anchor=2;e.focus=0;PocketTextEffect effect;
    CHECK(pocket_text_apply(&s,&e,&effect)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_INSERT,"B")==POCKET_TEXT_OK && value(&s,"BA"));
    CHECK(send(&s,POCKET_TEXT_SELECT_ALL,NULL)==POCKET_TEXT_OK && send(&s,POCKET_TEXT_CLEAR,NULL)==POCKET_TEXT_OK && value(&s,""));
    CHECK(send(&s,POCKET_TEXT_BACKSPACE,NULL)==POCKET_TEXT_OK && send(&s,POCKET_TEXT_DELETE,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_INSERT,"👩‍👩‍👧‍👦")==POCKET_TEXT_OK && snapshot(&s).graphemes==1);
    CHECK(send(&s,POCKET_TEXT_BACKSPACE,NULL)==POCKET_TEXT_OK && value(&s,""));
    pocket_text_dispose(&s);++groups;return 1;
}
static int encoding(void) {
    const char *str="Ae\xcc\x81😀🇨🇳Z";
    unsigned bytes[]={0,1,4,8,16,17},units[]={0,1,3,5,9,10};uint32_t out;
    for (unsigned i=0;i<6;i++) {
        CHECK(pocket_text_convert(str,strlen(str),POCKET_TEXT_GRAPHEME,i,POCKET_TEXT_UTF8_BYTE,&out)==POCKET_TEXT_OK && out==bytes[i]);
        CHECK(pocket_text_convert(str,strlen(str),POCKET_TEXT_UTF16_UNIT,units[i],POCKET_TEXT_GRAPHEME,&out)==POCKET_TEXT_OK && out==i);
    }
    CHECK(pocket_text_convert(str,strlen(str),POCKET_TEXT_UTF16_UNIT,2,POCKET_TEXT_GRAPHEME,&out)==POCKET_TEXT_INVALID_BOUNDARY);
    CHECK(pocket_text_convert(str,strlen(str),POCKET_TEXT_UTF16_UNIT,4,POCKET_TEXT_GRAPHEME,&out)==POCKET_TEXT_INVALID_BOUNDARY);
    CHECK(pocket_text_convert(str,strlen(str),POCKET_TEXT_UTF8_BYTE,3,POCKET_TEXT_GRAPHEME,&out)==POCKET_TEXT_INVALID_BOUNDARY);
    const char *bad[]={"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f","\xff"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) CHECK(pocket_text_graphemes(bad[i],strlen(bad[i]),&out)==POCKET_TEXT_INVALID_UTF8);
    PocketTextSession s={0};CHECK(begin(&s,"a",pocket_text_config_default()));
    PocketTextEvent e=event(&s,POCKET_TEXT_INSERT,bad[0]);CHECK(unchanged(&s,&e,POCKET_TEXT_INVALID_UTF8));
    e=event(&s,POCKET_TEXT_INSERT,"\r");CHECK(unchanged(&s,&e,POCKET_TEXT_POLICY));
    e=event(&s,POCKET_TEXT_SELECT,NULL);e.unit=POCKET_TEXT_UTF16_UNIT;e.focus=2;CHECK(unchanged(&s,&e,POCKET_TEXT_INVALID_BOUNDARY));
    e=event(&s,POCKET_TEXT_INSERT,"ok");e.version=0;CHECK(unchanged(&s,&e,POCKET_TEXT_INVALID_ARGUMENT));
    pocket_text_dispose(&s);++groups;return 1;
}
static int cluster_join_and_limits(void) {
    PocketTextSession s={0};PocketTextConfig c=pocket_text_config_default();c.max_graphemes=2;
    CHECK(begin(&s,"ab",c));CHECK(send(&s,POCKET_TEXT_LEFT,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_INSERT,"\xcc\x81")==POCKET_TEXT_OK && value(&s,"a\xcc\x81" "b") && snapshot(&s).focus_grapheme==1);
    PocketTextEvent e=event(&s,POCKET_TEXT_INSERT,"z");CHECK(unchanged(&s,&e,POCKET_TEXT_LIMIT));pocket_text_dispose(&s);
    CHECK(begin(&s,"👩👩",c));CHECK(send(&s,POCKET_TEXT_LEFT,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_INSERT,"‍")==POCKET_TEXT_OK && snapshot(&s).graphemes==1 && snapshot(&s).focus_grapheme==1);
    pocket_text_dispose(&s);c.max_bytes=2;c.max_graphemes=2;CHECK(begin(&s,"a",c));
    e=event(&s,POCKET_TEXT_INSERT,"中");CHECK(unchanged(&s,&e,POCKET_TEXT_LIMIT));
    char out[2]={42,43};size_t n=99;CHECK(pocket_text_copy_committed(&s,out,1,&n)==POCKET_TEXT_LIMIT && out[0]==42 && n==99);
    pocket_text_dispose(&s);c=pocket_text_config_default();c.max_graphemes=4096;
    char large[4097];memset(large,'x',4096);large[4096]=0;CHECK(begin(&s,large,c));
    CHECK(snapshot(&s).graphemes==4096);e=event(&s,POCKET_TEXT_INSERT,"x");CHECK(unchanged(&s,&e,POCKET_TEXT_LIMIT));
    CHECK(send(&s,POCKET_TEXT_CLEAR,NULL)==POCKET_TEXT_OK && value(&s,""));pocket_text_dispose(&s);++groups;return 1;
}
static int composition(void) {
    PocketTextSession s={0};CHECK(begin(&s,"old",pocket_text_config_default()));
    CHECK(send(&s,POCKET_TEXT_SELECT_ALL,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_COMPOSITION_BEGIN,NULL)==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_COMPOSITION_UPDATE,"ni hao")==POCKET_TEXT_OK && value(&s,"old"));
    char pre[32];size_t n;CHECK(pocket_text_copy_preedit(&s,pre,sizeof(pre),&n)==POCKET_TEXT_OK && n==6);
    PocketTextEvent edit=event(&s,POCKET_TEXT_BACKSPACE,NULL);CHECK(unchanged(&s,&edit,POCKET_TEXT_COMPOSING));
    PocketTextEvent enter=event(&s,POCKET_TEXT_ENTER,NULL);PocketTextEffect effect;
    CHECK(pocket_text_apply(&s,&enter,&effect)==POCKET_TEXT_OK && effect==POCKET_TEXT_EFFECT_ACCEPT_CANDIDATE && value(&s,"old"));
    PocketTextEvent commit=event(&s,POCKET_TEXT_COMPOSITION_COMMIT,"你好");
    CHECK(pocket_text_apply(&s,&commit,&effect)==POCKET_TEXT_OK && effect==POCKET_TEXT_EFFECT_COMMITTED && value(&s,"你好"));
    CHECK(!snapshot(&s).composing && !snapshot(&s).preedit_bytes);
    CHECK(unchanged(&s,&commit,POCKET_TEXT_STALE_EVENT));
    enter=event(&s,POCKET_TEXT_ENTER,NULL);CHECK(pocket_text_apply(&s,&enter,&effect)==POCKET_TEXT_OK && effect==POCKET_TEXT_EFFECT_SUBMIT);
    CHECK(unchanged(&s,&enter,POCKET_TEXT_STALE_EVENT));
    CHECK(send(&s,POCKET_TEXT_COMPOSITION_BEGIN,NULL)==POCKET_TEXT_OK && send(&s,POCKET_TEXT_COMPOSITION_UPDATE,"pending")==POCKET_TEXT_OK);
    CHECK(send(&s,POCKET_TEXT_COMPOSITION_CANCEL,NULL)==POCKET_TEXT_OK && value(&s,"你好") && !snapshot(&s).composing);
    pocket_text_dispose(&s);++groups;return 1;
}
static int stale_and_lifecycle(void) {
    PocketTextSession a={0},b={0};CHECK(begin(&a,"a",pocket_text_config_default()) && begin(&b,"b",pocket_text_config_default()));
    PocketTextEvent e=event(&a,POCKET_TEXT_INSERT,"late");CHECK(unchanged(&b,&e,POCKET_TEXT_STALE_EVENT));
    CHECK(pocket_text_blur(&a)==POCKET_TEXT_OK);CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    e=event(&a,POCKET_TEXT_INSERT,"late");CHECK(unchanged(&a,&e,POCKET_TEXT_NOT_FOCUSED));
    CHECK(pocket_text_focus(&a)==POCKET_TEXT_OK && send(&a,POCKET_TEXT_COMPOSITION_BEGIN,NULL)==POCKET_TEXT_OK);
    CHECK(send(&a,POCKET_TEXT_COMPOSITION_UPDATE,"preedit")==POCKET_TEXT_OK);
    e=event(&a,POCKET_TEXT_COMPOSITION_COMMIT,"late");
    CHECK(pocket_text_engine_reset(&a)==POCKET_TEXT_OK && !snapshot(&a).composing);
    CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    e=event(&a,POCKET_TEXT_INSERT,"late");++e.token.engine_generation;CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    e=event(&a,POCKET_TEXT_INSERT,"late");++e.token.field_id;CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    e=event(&a,POCKET_TEXT_INSERT,"late");pocket_text_dispose(&a);CHECK(begin(&a,"new",pocket_text_config_default()));
    CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    for (unsigned i=0;i<1000;i++) {
        e=event(&a,POCKET_TEXT_INSERT,"late");CHECK(pocket_text_blur(&a)==POCKET_TEXT_OK && pocket_text_focus(&a)==POCKET_TEXT_OK);
        CHECK(unchanged(&a,&e,POCKET_TEXT_STALE_EVENT));
    }
    CHECK(value(&a,"new") && value(&b,"b"));pocket_text_dispose(&a);pocket_text_dispose(&b);++groups;return 1;
}
static int policies(void) {
    PocketTextSession s={0};PocketTextConfig c=pocket_text_config_default();c.sensitive=1;
    const char *secret="  P\xcc\x81" "aß😀  ";CHECK(begin(&s,secret,c));char display[64];size_t n;
    CHECK(pocket_text_copy_display(&s,display,sizeof(display),&n)==POCKET_TEXT_OK && !strcmp(display,"********"));
    CHECK(value(&s,secret));PocketTextEvent e=event(&s,POCKET_TEXT_COMPOSITION_BEGIN,NULL);CHECK(unchanged(&s,&e,POCKET_TEXT_POLICY));
    CHECK(pocket_text_copy_preedit(&s,display,sizeof(display),&n)==POCKET_TEXT_POLICY);
    CHECK(pocket_text_set_flags(&s,1,1)==POCKET_TEXT_OK);e=event(&s,POCKET_TEXT_INSERT,"x");CHECK(unchanged(&s,&e,POCKET_TEXT_READ_ONLY));
    CHECK(send(&s,POCKET_TEXT_SELECT_ALL,NULL)==POCKET_TEXT_OK);
    CHECK(pocket_text_set_flags(&s,0,0)==POCKET_TEXT_OK);e=event(&s,POCKET_TEXT_INSERT,"x");CHECK(unchanged(&s,&e,POCKET_TEXT_DISABLED));
    CHECK(pocket_text_focus(&s)==POCKET_TEXT_DISABLED);CHECK(pocket_text_set_flags(&s,1,0)==POCKET_TEXT_OK);
    e=event(&s,POCKET_TEXT_INSERT,"x");CHECK(unchanged(&s,&e,POCKET_TEXT_NOT_FOCUSED));pocket_text_dispose(&s);
    c=pocket_text_config_default();c.mode=POCKET_TEXT_PIN;CHECK(begin(&s,"0012",c));CHECK(snapshot(&s).sensitive);
    e=event(&s,POCKET_TEXT_INSERT,".");CHECK(unchanged(&s,&e,POCKET_TEXT_POLICY));
    e=event(&s,POCKET_TEXT_INSERT,"１");CHECK(unchanged(&s,&e,POCKET_TEXT_POLICY));CHECK(value(&s,"0012"));pocket_text_dispose(&s);
    c.mode=POCKET_TEXT_ASCII;CHECK(begin(&s," A ",c));e=event(&s,POCKET_TEXT_INSERT,"é");CHECK(unchanged(&s,&e,POCKET_TEXT_POLICY));
    CHECK(value(&s," A "));pocket_text_dispose(&s);++groups;return 1;
}
static int conformance(const char *path) {
    FILE *f=fopen(path,"r");CHECK(f);unsigned cases=0,bytes,count;
    while (fscanf(f,"%u %u",&bytes,&count)==2) {
        CHECK(bytes<=4096 && count>0 && count<=4097);char text[4097];
        for(unsigned i=0;i<bytes;i++){unsigned x;CHECK(fscanf(f,"%x",&x)==1 && x<=255);text[i]=(char)x;}
        uint32_t actual;CHECK(pocket_text_graphemes(text,bytes,&actual)==POCKET_TEXT_OK && actual+1==count);
        for(unsigned i=0;i<count;i++) {
            unsigned position;CHECK(fscanf(f,"%u",&position)==1);
            CHECK(pocket_text_convert(text,bytes,POCKET_TEXT_GRAPHEME,i,POCKET_TEXT_UTF8_BYTE,&actual)==POCKET_TEXT_OK && actual==position);
        }
        ++cases;
    }
    CHECK(feof(f) && !ferror(f) && fclose(f)==0 && cases>500);
    printf("TEXT_UNICODE_OK version=%s cases=%u\n",pocket_text_unicode_version(),cases);++groups;return 1;
}
int main(int argc,char **argv) {
    if(argc!=2 && argc!=3)return 2;
    if(argc==3){
        uint32_t wrong;
        if(pocket_text_graphemes("ab",2,&wrong)!=POCKET_TEXT_OK)return 2;
        if(wrong!=1){fputs("TEXT_INTENTIONAL_NEGATIVE expected_one_for_two_graphemes\n",stderr);return 1;}
        return 0;
    }
    if(strcmp(pocket_text_unicode_version(),"17.0.0")){fputs("UNICODE_VERSION_MISMATCH\n",stderr);return 1;}
    if(!editing()||!encoding()||!cluster_join_and_limits()||!composition()||!stale_and_lifecycle()||!policies()||!conformance(argv[1]))return 1;
    printf("TEXT_SESSION_OK groups=%u editing encoding composition fencing policies conformance\n",groups);
    return 0;
}
