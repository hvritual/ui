/* Synthetic dependency feasibility probe, never a production input logger. */
#include <cstdio>
#include <cstring>
#include "pinyinime.h"
int main(int argc,char **argv){
    using namespace ime_pinyin;
    if(argc!=2)return 2;
    if(!im_open_decoder(argv[1],nullptr)){std::fputs("DECODER_OPEN_FAILED\n",stderr);return 1;}
    im_set_max_lens(32,16);
    if(im_is_user_dictionary_enabled()){im_close_decoder();return 1;}
    const char *samples[]={"nihao","zhongguo","kafei","shanghai"};
    for(const char *s:samples){
        im_reset_search();size_t n=im_search(s,std::strlen(s)),decoded=0;
        im_get_sps_str(&decoded);char16 candidate[33]={0};
        if(!n||decoded!=std::strlen(s)||!im_get_candidate(0,candidate,33)||!candidate[0]){im_close_decoder();return 1;}
        std::printf("SYNTHETIC_PINYIN_OK count=%zu decoded=%zu first=%04x second=%04x\n",n,decoded,unsigned(candidate[0]),unsigned(candidate[1]));
    }
    im_close_decoder();std::puts("PINYIN_STANDALONE_OK ui_integrated=false physical=false");return 0;
}
