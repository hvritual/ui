#include "layout.h"
#include <string.h>
static int valid(const PocketKeyLayoutState *s){return s&&s->generation&&!(s->available_locales&~3U)&&s->active_locales&&!(s->active_locales&~s->available_locales)&&(s->locale==1||s->locale==2)&&(s->locale&s->active_locales)&&s->mode>=POCKET_LAYOUT_LETTERS&&s->mode<=POCKET_LAYOUT_DIGITS&&(!s->sensitive||s->locale==1);}
static int next(PocketKeyLayoutState *s){if(!valid(s)||s->generation==UINT64_MAX)return 0;s->generation++;return 1;}
int pocket_key_layout_init(PocketKeyLayoutState *s,unsigned available,unsigned active,unsigned initial,int sensitive){
    if(!s)return 0;
    PocketKeyLayoutState value={available,active,initial,POCKET_LAYOUT_LETTERS,(uint8_t)!!sensitive,0,0,1};
    if(sensitive)value.active_locales&=POCKET_KEY_LAYOUT_EN;
    if(!valid(&value))return 0;
    *s=value;return 1;
}
int pocket_key_layout_shift(PocketKeyLayoutState *s,int shifted){if(!valid(s)||s->mode!=POCKET_LAYOUT_LETTERS||!next(s))return 0;s->shift=!!shifted;return 1;}
int pocket_key_layout_mode(PocketKeyLayoutState *s,PocketKeyLayoutMode mode){if(mode>POCKET_LAYOUT_DIGITS||mode<POCKET_LAYOUT_LETTERS||!next(s))return 0;s->mode=mode;s->popup=0;s->shift=0;return 1;}
int pocket_key_layout_languages(PocketKeyLayoutState *s,int show){if(!valid(s)||s->sensitive||s->mode==POCKET_LAYOUT_DIGITS||!next(s))return 0;s->popup=!!show;return 1;}
int pocket_key_layout_choose(PocketKeyLayoutState *s,unsigned locale){
    if(!valid(s)||!s->popup||!(locale&s->active_locales)||(locale!=1&&locale!=2)||!next(s))return 0;
    s->locale=locale;s->popup=0;s->shift=0;return 1;
}
static void row(PocketKeyLayout *out,int y,const char *characters,int functions){
    /* Keys use row-relative weights, as in KeyboardRow/KeyboardLayout. The
     * symbol list is ours; no Qt QML/source/assets are copied. */
    unsigned n=(unsigned)strlen(characters),weight=n+(functions?4U:0U);int available=992-(int)(n+(functions?2:0)-1)*8;
    int x=16;
    if(functions){int w=available*2/(int)weight;out->keys[out->count++]=(PocketKeyGeometry){POCKET_KEY_SHIFT,0,{x,y,w,52}};x+=w+8;}
    for(unsigned j=0;j<n;j++){int w=available/(int)weight;out->keys[out->count++]=(PocketKeyGeometry){POCKET_KEY_CHAR,(unsigned char)characters[j],{x,y,w,52}};x+=w+8;}
    if(functions)out->keys[out->count++]=(PocketKeyGeometry){POCKET_KEY_BACKSPACE,0,{x,y,1008-x,52}};
}
int pocket_key_layout_build(const PocketKeyLayoutState *s,unsigned height,PocketKeyLayout *out){
    if(!valid(s)||!out||(height!=600&&height!=800))return 0;
    memset(out,0,sizeof(*out));
    out->generation=s->generation;out->space_locale=s->locale;out->editor=(PocketKeyRect){32,144,960,64};
    out->preedit=(PocketKeyRect){32,(int)height-364,960,32};out->candidates=(PocketKeyRect){16,(int)height-320,992,52};
    out->language_popup=(PocketKeyRect){288,(int)height-256,448,172};
    for(unsigned mask=1;mask<=2;mask<<=1)if(mask&s->active_locales)out->language_choices[out->language_count++]=mask;
    int y=(int)height-256;
    if(s->mode==POCKET_LAYOUT_DIGITS){row(out,y,"123",0);row(out,y+60,"456",0);row(out,y+120,"789",0);}
    else if(s->mode==POCKET_LAYOUT_SYMBOLS){row(out,y,"1234567890",0);row(out,y+60,"!@#$%&*()",0);row(out,y+120,"-_=+.,?",1);}
    else {row(out,y,"qwertyuiop",0);row(out,y+60,"asdfghjkl",0);row(out,y+120,"zxcvbnm",1);}
    PocketKeyRole roles[]={POCKET_KEY_MODE,POCKET_KEY_LANGUAGE,POCKET_KEY_SPACE,POCKET_KEY_ENTER,POCKET_KEY_HIDE};
    const int widths[]={136,104,432,160,128};int x=16;
    for(unsigned i=0;i<5;i++){
        PocketKeyRole role=roles[i];uint32_t cp=role==POCKET_KEY_SPACE?32:0;
        if(s->mode==POCKET_LAYOUT_DIGITS){if(i==1)role=POCKET_KEY_BACKSPACE;if(i==2){role=POCKET_KEY_CHAR;cp='0';}}
        if(s->sensitive&&i==1&&s->mode!=POCKET_LAYOUT_DIGITS){x+=widths[i]+8;continue;}
        out->keys[out->count++]=(PocketKeyGeometry){role,cp,{x,y+180,widths[i],52}};x+=widths[i]+8;
    }
    if(s->shift&&s->mode==POCKET_LAYOUT_LETTERS)for(unsigned i=0;i<out->count;i++)if(out->keys[i].role==POCKET_KEY_CHAR&&out->keys[i].codepoint>='a'&&out->keys[i].codepoint<='z')out->keys[i].codepoint-=32;
    return out->count<=POCKET_KEY_LAYOUT_MAX_KEYS;
}
