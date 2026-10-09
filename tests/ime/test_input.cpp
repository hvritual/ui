/* Only synthetic text is used here. Production provider/controller have no logger. */
extern "C" {
#include "hosts/linux/ime/session.h"
#include "hosts/linux/text-input/layout.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cerrno>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"IME_ASSERT line=%d expression=%s\n",__LINE__,#x);std::exit(1);}}while(0)
static unsigned groups=0;
static void pass(const char *name){std::printf("PASS %s\n",name);groups++;}
static uint64_t now(){timespec t{};CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return uint64_t(t.tv_sec)*1000+uint64_t(t.tv_nsec)/1000000;}
static PocketTextToken token(PocketTextSession *text){PocketTextSnapshot s{};CHECK(pocket_text_snapshot(text,&s)==POCKET_TEXT_OK);return s.token;}
static void value(PocketTextSession *text,const char *expected){char b[4097];size_t n;CHECK(pocket_text_copy_committed(text,b,sizeof(b),&n)==POCKET_TEXT_OK);CHECK(n==std::strlen(expected)&&std::memcmp(b,expected,n)==0);}
static void type(PocketImeSession *ime,PocketTextSession *text,const char *s){for(;*s;s++)CHECK(pocket_ime_key(ime,token(text),*s,now())==POCKET_TEXT_OK);}
static PocketPinyinResult wait(PocketImeSession *ime){
    uint64_t deadline=now()+4000;
    for(;;){PocketPinyinStatus s=pocket_ime_step(ime,now());CHECK(s==POCKET_PINYIN_OK||s==POCKET_PINYIN_PENDING);
        PocketImeSnapshot snap{};CHECK(pocket_ime_snapshot(ime,&snap));
        if(!snap.pending){PocketPinyinResult r{};CHECK(pocket_ime_candidates(ime,&r));return r;}
        CHECK(now()<deadline);usleep(1000);
    }
}
struct Editing {
    PocketTextSession text{};PocketImeSession ime{};
    Editing(const std::vector<unsigned char> &dict,unsigned max=64){PocketTextConfig config=pocket_text_config_default();config.max_bytes=256;config.max_graphemes=max;
        CHECK(pocket_text_init(&text,123,&config,"",0)==POCKET_TEXT_OK);CHECK(pocket_text_focus(&text)==POCKET_TEXT_OK);
        CHECK(pocket_ime_open(&ime,&text,dict.data(),dict.size()));CHECK(pocket_ime_locale(&ime,token(&text),POCKET_INPUT_ZH_CN,now())==POCKET_TEXT_OK);}
    ~Editing(){pocket_ime_close(&ime);pocket_text_dispose(&text);}
};
static void layout_tests(){
    for(unsigned height:{600U,800U})for(unsigned locale:{1U,2U})for(int mode=0;mode<3;mode++){
        PocketKeyLayoutState s{};CHECK(pocket_key_layout_init(&s,3,3,locale,0));CHECK(pocket_key_layout_mode(&s,PocketKeyLayoutMode(mode)));
        PocketKeyLayout out{};CHECK(pocket_key_layout_build(&s,height,&out));CHECK(out.count>=10&&out.count<=48&&out.space_locale==locale);
        CHECK(out.editor.y+out.editor.height<=out.preedit.y);CHECK(out.preedit.y+out.preedit.height<=out.candidates.y);
        for(unsigned n=0;n<out.count;n++){PocketKeyRect r=out.keys[n].bounds;CHECK(r.x>=0&&r.y>=out.candidates.y+out.candidates.height&&r.width>=48&&r.height>=48&&r.x+r.width<=1024&&r.y+r.height<=int(height));
            if(mode==POCKET_LAYOUT_DIGITS)CHECK(out.keys[n].role!=POCKET_KEY_SHIFT&&out.keys[n].role!=POCKET_KEY_LANGUAGE);
            for(unsigned j=0;j<n;j++){PocketKeyRect q=out.keys[j].bounds;CHECK(r.x+r.width<=q.x||q.x+q.width<=r.x||r.y+r.height<=q.y||q.y+q.height<=r.y);}
        }
    }
    PocketKeyLayoutState s{};CHECK(!pocket_key_layout_init(&s,1,3,1,0));CHECK(!pocket_key_layout_init(&s,3,3,2,1));
    CHECK(pocket_key_layout_init(&s,3,3,1,0));uint64_t generation=s.generation;CHECK(pocket_key_layout_languages(&s,1));CHECK(pocket_key_layout_choose(&s,2));CHECK(s.locale==2&&s.generation>generation&&!s.popup);
    CHECK(!pocket_key_layout_choose(&s,1));CHECK(pocket_key_layout_init(&s,3,3,1,1));CHECK(!pocket_key_layout_languages(&s,1));
    CHECK(pocket_key_layout_shift(&s,1));PocketKeyLayout shifted{};CHECK(pocket_key_layout_build(&s,600,&shifted));CHECK(shifted.keys[0].codepoint=='Q');
    CHECK(!pocket_key_layout_mode(&s,PocketKeyLayoutMode(-1)));s.generation=UINT64_MAX;CHECK(!pocket_key_layout_shift(&s,0));
    pass("weighted-layouts-dual-target-language-popup-policy");
}
static void provider_tests(const std::vector<unsigned char> &dict){
    PocketPinyin p{};auto bad=dict;bad[100]^=1;CHECK(pocket_pinyin_open(&p,bad.data(),bad.size(),now())==POCKET_PINYIN_UNAVAILABLE&&!p.impl);
    CHECK(pocket_pinyin_open(&p,dict.data(),dict.size()-1,now())==POCKET_PINYIN_UNAVAILABLE&&!p.impl);
    CHECK(!setenv("POCKET_PINYIN_TEST_MEMFD_DENIED","1",1));
    CHECK(pocket_pinyin_open(&p,dict.data(),dict.size(),now())==POCKET_PINYIN_UNAVAILABLE&&!p.impl);
    int denied_stage=0,denied_errno=0;unsigned denied_storage=0;
    pocket_pinyin_diagnostics(&p,&denied_stage,&denied_errno,&denied_storage);
    CHECK(denied_stage==POCKET_PINYIN_STAGE_MEMFD&&denied_errno==EPERM&&denied_storage==POCKET_PINYIN_STORAGE_NONE);
    CHECK(!unsetenv("POCKET_PINYIN_TEST_MEMFD_DENIED"));
    CHECK(!setenv("POCKET_PINYIN_FORCE_UNLINKED_FILE","1",1));
    uint64_t fallback_start=now();CHECK(pocket_pinyin_open(&p,dict.data(),dict.size(),fallback_start)==POCKET_PINYIN_OK);
    int stage=0,system_errno=0;unsigned storage=0;pocket_pinyin_diagnostics(&p,&stage,&system_errno,&storage);
    CHECK(stage==POCKET_PINYIN_STAGE_NONE&&system_errno==0&&storage==POCKET_PINYIN_STORAGE_UNLINKED_FILE);
    pocket_pinyin_close(&p);
    {
        Editing e(dict);
        type(&e.ime,&e.text,"nihao");
        auto candidates=wait(&e.ime);
        CHECK(candidates.count&&std::strcmp(candidates.candidates[0].text,"你好")==0);
        PocketImeSnapshot snap{};
        CHECK(pocket_ime_snapshot(&e.ime,&snap)&&snap.provider_storage==POCKET_PINYIN_STORAGE_UNLINKED_FILE);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),candidates.request,0,now())==POCKET_TEXT_OK);
        value(&e.text,"你好");
    }
    CHECK(!unsetenv("POCKET_PINYIN_FORCE_UNLINKED_FILE"));
    uint64_t start=now();CHECK(pocket_pinyin_open(&p,dict.data(),dict.size(),start)==POCKET_PINYIN_OK);
    PocketPinyinResult r{};CHECK(pocket_pinyin_poll(&p,start+POCKET_PINYIN_TIMEOUT_MS,&r)==POCKET_PINYIN_TIMEOUT);CHECK(!r.count);pocket_pinyin_close(&p);
    CHECK(pocket_pinyin_open(&p,dict.data(),dict.size(),now())==POCKET_PINYIN_OK);
    PocketTextToken t={1,1,1,1,0};uint64_t id=0;
    CHECK(pocket_pinyin_request(&p,t,"ABC",3,0,now(),&id)==POCKET_PINYIN_INVALID);
    CHECK(pocket_pinyin_request(&p,t,"nihao",5,0,now(),&id)==POCKET_PINYIN_OK);
    for(unsigned j=0;j<50;j++)CHECK(pocket_pinyin_request(&p,t,j%2?"zhongguo":"kafei",j%2?8:5,0,now(),&id)==POCKET_PINYIN_OK);
    uint64_t end=now()+4000;for(;;){PocketPinyinStatus status=pocket_pinyin_poll(&p,now(),&r);CHECK(status==POCKET_PINYIN_OK||status==POCKET_PINYIN_PENDING);if(status==POCKET_PINYIN_OK)break;CHECK(now()<end);usleep(1000);}
    CHECK(r.request==id&&r.count&&std::strcmp(r.candidates[0].text,"中国")==0);
    pocket_pinyin_cancel(&p);pocket_pinyin_close(&p);pocket_pinyin_close(&p);
    CHECK(pocket_pinyin_open(&p,dict.data(),dict.size(),now())==POCKET_PINYIN_OK);
    uint64_t future=now()+100;CHECK(pocket_pinyin_request(&p,t,"nihao",5,0,future,&id)==POCKET_PINYIN_OK);
    CHECK(pocket_pinyin_poll(&p,future-1,&r)==POCKET_PINYIN_INVALID&&r.count==0);
    pocket_pinyin_close(&p);
    pass("pinned-dictionary-timeout-newest-only-bounded-worker");
}
static void failure_tests(const std::vector<unsigned char> &dict){
    {
        PocketTextSession text{};PocketImeSession ime{};PocketTextConfig c=pocket_text_config_default();
        CHECK(pocket_text_init(&text,456,&c,"old",3)==POCKET_TEXT_OK);CHECK(pocket_text_focus(&text)==POCKET_TEXT_OK);
        CHECK(pocket_ime_open(&ime,&text,nullptr,0));auto before=token(&text);
        CHECK(pocket_ime_locale(&ime,before,POCKET_INPUT_ZH_CN,now())==POCKET_TEXT_POLICY);
        CHECK(token(&text).revision==before.revision&&token(&text).engine_generation==before.engine_generation);value(&text,"old");
        PocketImeSnapshot snap{};CHECK(pocket_ime_snapshot(&ime,&snap)&&snap.locale==POCKET_INPUT_EN_US&&snap.provider_status==POCKET_PINYIN_UNAVAILABLE);
        pocket_ime_close(&ime);pocket_text_dispose(&text);
    }
    {
        Editing e(dict);type(&e.ime,&e.text,"nihao");CHECK(pocket_ime_step(&e.ime,now()+POCKET_PINYIN_TIMEOUT_MS)==POCKET_PINYIN_TIMEOUT);
        PocketImeSnapshot snap{};CHECK(pocket_ime_snapshot(&e.ime,&snap)&&!snap.pending&&snap.provider_status==POCKET_PINYIN_TIMEOUT);value(&e.text,"");
        auto stale=token(&e.text);CHECK(pocket_ime_locale(&e.ime,stale,POCKET_INPUT_EN_US,now())==POCKET_TEXT_OK);
        CHECK(pocket_ime_locale(&e.ime,token(&e.text),POCKET_INPUT_ZH_CN,now())==POCKET_TEXT_OK);
        type(&e.ime,&e.text,"nihao");auto r=wait(&e.ime);CHECK(r.count);
        CHECK(pocket_ime_choose(&e.ime,stale,r.request,0,now())==POCKET_TEXT_STALE_EVENT);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),r.request,0,now())==POCKET_TEXT_OK);value(&e.text,"你好");
    }
    pass("explicit-unavailable-timeout-restart-no-silent-fallback");
}
static void editor_tests(const std::vector<unsigned char> &dict){
    {
        Editing e(dict);type(&e.ime,&e.text,"nihao");value(&e.text,"");auto r=wait(&e.ime);CHECK(r.count&&std::strcmp(r.candidates[0].text,"你好")==0);
        auto old=token(&e.text);PocketTextEffect effect;
        CHECK(pocket_ime_enter(&e.ime,old,now(),&effect)==POCKET_TEXT_OK&&effect==POCKET_TEXT_EFFECT_COMMITTED);value(&e.text,"你好");
        CHECK(pocket_ime_choose(&e.ime,old,r.request,0,now())==POCKET_TEXT_STALE_EVENT);
        CHECK(pocket_ime_enter(&e.ime,token(&e.text),now(),&effect)==POCKET_TEXT_OK&&effect==POCKET_TEXT_EFFECT_SUBMIT);
        CHECK(pocket_ime_backspace(&e.ime,token(&e.text),now())==POCKET_TEXT_OK);value(&e.text,"你");
    }pass("real-candidates-single-commit-grapheme-backspace");
    {
        Editing e(dict);type(&e.ime,&e.text,"ni");auto r=wait(&e.ime);CHECK(r.total>5);
        CHECK(pocket_ime_page(&e.ime,token(&e.text),1,now())==POCKET_TEXT_OK);auto next=wait(&e.ime);CHECK(next.page==1&&next.count);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),r.request,0,now())==POCKET_TEXT_STALE_EVENT);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),next.request,0,now())==POCKET_TEXT_OK);
    }pass("candidate-page-stale-selection-rejected");
    {
        Editing e(dict);type(&e.ime,&e.text,"nihao");auto r=wait(&e.ime);unsigned partial=99;
        for(unsigned i=0;i<r.count;i++)if(r.candidates[i].consumed_bytes<5){partial=i;break;}
        CHECK(partial<r.count);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),r.request,partial,now())==POCKET_TEXT_OK);value(&e.text,"");auto second=wait(&e.ime);CHECK(second.count);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),second.request,0,now())==POCKET_TEXT_OK);
        PocketTextSnapshot s{};CHECK(pocket_text_snapshot(&e.text,&s)==POCKET_TEXT_OK&&!s.composing&&s.graphemes==2);
    }pass("partial-choice-preserves-unconsumed-spelling");
    {
        Editing e(dict);type(&e.ime,&e.text,"nihao");auto old=token(&e.text);
        CHECK(pocket_ime_locale(&e.ime,old,POCKET_INPUT_EN_US,now())==POCKET_TEXT_OK);value(&e.text,"");
        CHECK(pocket_ime_key(&e.ime,old,'x',now())==POCKET_TEXT_STALE_EVENT);type(&e.ime,&e.text,"ABC");value(&e.text,"ABC");
        CHECK(pocket_ime_locale(&e.ime,token(&e.text),POCKET_INPUT_ZH_CN,now())==POCKET_TEXT_OK);type(&e.ime,&e.text,"kafei");
        CHECK(pocket_text_blur(&e.text)==POCKET_TEXT_OK);CHECK(pocket_ime_step(&e.ime,now())==POCKET_PINYIN_PENDING);
        CHECK(pocket_text_focus(&e.text)==POCKET_TEXT_OK);CHECK(pocket_ime_step(&e.ime,now())==POCKET_PINYIN_PENDING);value(&e.text,"ABC");
        type(&e.ime,&e.text,"zhongguo");auto r=wait(&e.ime);CHECK(std::strcmp(r.candidates[0].text,"中国")==0);
    }pass("language-focus-generations-discard-late-input");
    {
        Editing e(dict,1);type(&e.ime,&e.text,"nihao");auto r=wait(&e.ime);
        CHECK(pocket_ime_choose(&e.ime,token(&e.text),r.request,0,now())==POCKET_TEXT_LIMIT);value(&e.text,"");
        CHECK(pocket_ime_cancel(&e.ime,token(&e.text))==POCKET_TEXT_OK);value(&e.text,"");
        type(&e.ime,&e.text,"xi'an");r=wait(&e.ime);CHECK(r.count&&r.decoded_bytes==5);
    }pass("limits-cancellation-apostrophe-segmentation");
    {
        PocketTextSession t{};PocketImeSession i{};PocketTextConfig c=pocket_text_config_default();c.sensitive=1;
        CHECK(pocket_text_init(&t,555,&c,"",0)==POCKET_TEXT_OK);CHECK(!pocket_ime_open(&i,&t,dict.data(),dict.size()));pocket_text_dispose(&t);
        for(unsigned n=0;n<20;n++){Editing e(dict);type(&e.ime,&e.text,"kafei");auto r=wait(&e.ime);CHECK(r.count);}
        int st;CHECK(waitpid(-1,&st,WNOHANG)==-1&&errno==ECHILD);
    }pass("sensitive-policy-and-owned-worker-lifecycle");
}
int main(int argc,char **argv){
    CHECK(argc==2||argc==3);FILE *f=std::fopen(argv[1],"rb");CHECK(f);std::vector<unsigned char> data(1068442);CHECK(std::fread(data.data(),1,data.size(),f)==data.size()&&std::fgetc(f)==EOF);CHECK(!std::fclose(f));
    layout_tests();provider_tests(data);editor_tests(data);failure_tests(data);
    if(argc==3){CHECK(std::strcmp(argv[2],"--intentional-failure")==0);std::fputs("INTENTIONAL_IME_ASSERTION_FAILURE\n",stderr);return 1;}
    std::printf("IME_CORE_OK groups=%u rendered_ui=false physical=false\n",groups);return 0;
}
