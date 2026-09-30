#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/framework.h"
#include "frame_oracle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"KEYBOARD_FAIL line=%d %s\n",__LINE__,#x);return 0;}}while(0)
typedef struct {PocketFramework *r;uint64_t ms,seq;unsigned height;const char *out;} Driver;
static int step(Driver *d,unsigned delta){d->ms+=delta;CHECK(pocket_framework_tick(d->r,d->ms*1000000ULL,1));return 1;}
static int frame(Driver *d,const InputFrame *f){d->ms+=20;CHECK(pocket_framework_input(d->r,f,d->ms*1000000ULL));CHECK(step(d,1));return 1;}
static int contact(Driver *d,int down,int x,int y){
    InputFrame f={.sequence=++d->seq};if(down){f.contact_count=1;f.contacts[0]=(InputContact){0,x,y};}return frame(d,&f);
}
static int tap(Driver *d,int x,int y){CHECK(contact(d,1,x,y));CHECK(contact(d,0,x,y));return 1;}
static int key(Driver *d,unsigned row,unsigned col){return tap(d,59+(int)col*62,(int)d->height-230+(int)row*60);}
static int stats(Driver *d,unsigned field){
    PocketKeyboardSnapshot s;CHECK(coffee_app_keyboard_snapshot(&d->r->app,&s));CHECK(s.result==POCKET_KEYBOARD_EDITING&&s.active_field==field);
    CoffeeAppStats a;CHECK(coffee_app_stats(&d->r->app,&a));CHECK(a.page==1&&a.modal==1&&a.nodes<256);return 1;
}
/* Synthetic fixtures only: capture actual Core pixels directly; never bypass
 * the production snapshot API's refusal for user-entered text. */
static int shot(Driver *d,const char *label){
    char p[4096];CHECK(snprintf(p,sizeof(p),"%s/%s.ppm",d->out,label)<(int)sizeof(p));
    FILE *f=fopen(p,"wx");CHECK(f);const PocketEngineFrame *im=&d->r->frame;
    CHECK(fprintf(f,"P6\n%u %u\n255\n",im->width,im->height)>0);
    for(unsigned y=0;y<im->height;y++)for(unsigned x=0;x<im->width;x++){
        const uint8_t *b=im->pixels+y*im->stride+x*4;unsigned char rgb[]={b[2],b[1],b[0]};CHECK(fwrite(rgb,1,3,f)==3);
    }
    CHECK(fclose(f)==0);return 1;
}
static int field_text(Driver *d,int sensitive,const char *expected){
    const PocketScene *s=&d->r->scene;unsigned count=0;
    for(unsigned n=0;n<s->count;n++){
        const PocketSceneRecord *r=&s->records[n];
        if(r->kind==POCKET_COMPONENT_TEXT&&r->bounds.y==216&&r->bounds.x>=48&&r->bounds.x<912){
            CHECK(r->text_ref>=1032&&r->text_ref<=1126);
            if(sensitive)CHECK(r->text_ref==1000+'*');
            else{unsigned pos=(unsigned)(r->bounds.x-48)/24;CHECK(pos<strlen(expected));CHECK(r->text_ref==1000U+(unsigned char)expected[pos]);}
            count++;
        }
    }
    CHECK(count==strlen(expected));return 1;
}
static int run(const char *assets,const char *out,unsigned height){
    Driver d={.r=calloc(1,sizeof(PocketFramework)),.ms=1000,.height=height};CHECK(d.r);
    char dir[4096],forbidden[4096];CHECK(snprintf(dir,sizeof(dir),"%s/keyboard-%u",out,height)<(int)sizeof(dir));CHECK(mkdir(dir,0700)==0);d.out=dir;
    FrameOracle oracle;CHECK(oracle_open(&oracle,1024,height));
    PocketDisplayBackend display={1,sizeof(PocketDisplayBackend),&oracle,oracle_present};
    CHECK(pocket_framework_open(d.r,height,8,assets,NULL,&display));CHECK(step(&d,0));
    CoffeeAppStats baseline;CHECK(coffee_app_stats(&d.r->app,&baseline));
    CHECK(tap(&d,675,(int)height-36));CHECK(stats(&d,0));CHECK(field_text(&d,0,"Coffee"));CHECK(shot(&d,"open"));
    CHECK(snprintf(forbidden,sizeof(forbidden),"%s/forbidden.ppm",dir)<(int)sizeof(forbidden));
    CHECK(!pocket_framework_snapshot(d.r,forbidden)&&access(forbidden,F_OK)!=0);
    CHECK(tap(&d,550,300)); /* All */
    CHECK(key(&d,0,0));CHECK(field_text(&d,0,"q"));
    CHECK(tap(&d,790,300)); /* Shift */ CHECK(key(&d,1,0));CHECK(field_text(&d,0,"qA"));
    CHECK(key(&d,1,1));CHECK(field_text(&d,0,"qAs"));
    CHECK(tap(&d,900,300)); /* Caps */CHECK(key(&d,0,1));CHECK(field_text(&d,0,"qAsW"));
    CHECK(tap(&d,900,300));
    CHECK(tap(&d,900,(int)height-230)); /* symbols */ CHECK(key(&d,0,4));CHECK(field_text(&d,0,"qAsW5"));CHECK(shot(&d,"symbols"));
    CHECK(tap(&d,70,300)); /* Home */ CHECK(key(&d,0,0));CHECK(field_text(&d,0,"1qAsW5"));
    CHECK(tap(&d,300,300)); /* Right */ CHECK(tap(&d,900,(int)height-170)); /* Backspace */CHECK(field_text(&d,0,"1AsW5"));
    CHECK(shot(&d,"edited"));
    /* Partial selection via Shift+arrows reuses the TextSession selection contract. */
    CHECK(tap(&d,420,300));CHECK(tap(&d,790,300));CHECK(tap(&d,180,300));CHECK(tap(&d,180,300));
    CHECK(key(&d,0,1));CHECK(field_text(&d,0,"1As2"));
    CHECK(tap(&d,550,300));CHECK(key(&d,0,0)); /* reset to a deterministic saved value */
    CHECK(field_text(&d,0,"1"));
    CHECK(tap(&d,900,(int)height-230));CHECK(tap(&d,790,300));CHECK(key(&d,1,0));CHECK(key(&d,1,1));
    CHECK(tap(&d,790,300));CHECK(key(&d,0,1));CHECK(tap(&d,900,(int)height-230));CHECK(key(&d,0,4));
    CHECK(field_text(&d,0,"1AsW5"));
    CHECK(tap(&d,400,140));CHECK(stats(&d,1));CHECK(key(&d,0,0));CHECK(key(&d,0,1));CHECK(field_text(&d,0,"12"));CHECK(shot(&d,"number"));
    CHECK(tap(&d,650,140));CHECK(stats(&d,2));CHECK(tap(&d,900,(int)height-230)); /* back letters */
    CHECK(key(&d,0,0));CHECK(key(&d,1,0));CHECK(field_text(&d,1,"**"));CHECK(shot(&d,"password"));
    CHECK(tap(&d,880,140));CHECK(stats(&d,3));CHECK(key(&d,0,0));CHECK(key(&d,0,1));CHECK(field_text(&d,1,"**"));CHECK(shot(&d,"pin"));
    CHECK(!pocket_framework_snapshot(d.r,forbidden)&&access(forbidden,F_OK)!=0);
    CHECK(tap(&d,560,40)); /* Hide */PocketKeyboardSnapshot k;CHECK(coffee_app_keyboard_snapshot(&d.r->app,&k)&&!k.visible);CHECK(shot(&d,"hidden"));
    CHECK(tap(&d,100,(int)height-180));CHECK(stats(&d,3)); /* old Coffee card cannot be hit */
    CHECK(tap(&d,560,40));CHECK(coffee_app_keyboard_snapshot(&d.r->app,&k)&&k.visible);
    CHECK(tap(&d,100,140));CHECK(stats(&d,0));CHECK(tap(&d,550,300));CHECK(shot(&d,"selected"));
    /* A contact begun over a key cannot edit a different field on late release. */
    InputFrame multi={.contact_count=1,.contacts={{0,59,(int)height-230}}};CHECK(frame(&d,&multi));
    multi.contact_count=2;multi.contacts[1]=(InputContact){1,400,140};CHECK(frame(&d,&multi));
    multi.contact_count=1;multi.contacts[0]=multi.contacts[1];CHECK(frame(&d,&multi));
    multi.contact_count=0;CHECK(frame(&d,&multi));CHECK(stats(&d,1));CHECK(field_text(&d,0,"12"));
    CHECK(contact(&d,1,900,40));
    InputFrame release={0};d.ms+=20;CHECK(pocket_framework_input(d.r,&release,d.ms*1000000ULL));
    CHECK(!coffee_app_keyboard_snapshot(&d.r->app,&k));
    CHECK(!pocket_framework_snapshot(d.r,forbidden)&&access(forbidden,F_OK)!=0);
    CHECK(step(&d,1));CHECK(pocket_framework_can_snapshot(d.r));
    CHECK(tap(&d,675,(int)height-36));CHECK(field_text(&d,0,"1AsW5"));
    CHECK(tap(&d,550,300));CHECK(key(&d,0,0));CHECK(field_text(&d,0,"q"));CHECK(tap(&d,720,40)); /* Cancel */
    CHECK(tap(&d,675,(int)height-36));CHECK(field_text(&d,0,"1AsW5"));
    CHECK(tap(&d,650,140));CHECK(field_text(&d,1,"")); /* secret did not survive close */
    CHECK(tap(&d,100,140));CHECK(tap(&d,420,300)); /* End */
    CHECK(contact(&d,1,900,(int)height-170));CHECK(step(&d,501));CHECK(step(&d,90));CHECK(contact(&d,0,900,(int)height-170));CHECK(field_text(&d,0,"1As"));
    CHECK(tap(&d,720,40));
    /* Cancellation through the existing input-loss path cannot type a ghost. */
    CHECK(tap(&d,675,(int)height-36));CHECK(contact(&d,1,59,(int)height-230));
    InputFrame lost={.syn_dropped=1,.suppressed=1};CHECK(frame(&d,&lost));CHECK(contact(&d,0,59,(int)height-230));CHECK(field_text(&d,0,"1AsW5"));
    CHECK(tap(&d,720,40));
    for(unsigned n=0;n<20;n++){CHECK(tap(&d,675,(int)height-36));CHECK(tap(&d,720,40));}
    CoffeeAppStats final;CHECK(coffee_app_stats(&d.r->app,&final));CHECK(final.nodes==baseline.nodes&&final.page==1&&final.modal==0);
    CHECK(tap(&d,100,200));CHECK(coffee_app_stats(&d.r->app,&final)&&final.page==2); /* old navigation still works */
    CHECK(pocket_framework_close(d.r));CHECK(host_alloc_stats().live_bytes==0);free(d.r);oracle_close(&oracle);
    puts("KEYBOARD_FLOW_OK synthetic-input real-F6 real-Core no-secret-traces");return 1;
}
int framework_keyboard_cases(const char *assets,const char *out){return run(assets,out,600)&&run(assets,out,800);}
