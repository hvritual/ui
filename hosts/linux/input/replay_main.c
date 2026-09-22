#define _POSIX_C_SOURCE 200809L
#include "input/state.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int int_arg(const char *s, int min, int max, int *out) {
    if (!s || !*s) return 0;
    errno=0; char *end=NULL; long v=strtol(s,&end,10);
    if(errno||!end||*end||v<min||v>max)return 0;
    *out=(int)v;return 1;
}
static void print_frame(const InputFrame *f) {
    printf("FRAME seq=%llu contacts=%u",
           (unsigned long long)f->sequence, f->contact_count);
    for(unsigned i=0;i<f->contact_count;i++)
        printf(" %d:%d:%d",f->contacts[i].id,f->contacts[i].x,f->contacts[i].y);
    printf(" cancels=%u",f->cancelled_count);
    for(unsigned i=0;i<f->cancelled_count;i++)printf(" %d",f->cancelled[i]);
    printf(" suppressed=%d dropped=%d\n",f->suppressed,f->syn_dropped);
    fflush(stdout);
}
int main(int argc,char **argv){
    InputProtocol protocol=0;
    int slots=0,width=0,height=0,xmin=0,xmax=0,ymin=0,ymax=0,swap=0,ix=0,iy=0;
    for(int i=1;i<argc;i+=2){
        if(i+1>=argc)goto args;
        if(!strcmp(argv[i],"--protocol")){
            if(!strcmp(argv[i+1],"mtb"))protocol=INPUT_PROTOCOL_MT_B;
            else if(!strcmp(argv[i+1],"single"))protocol=INPUT_PROTOCOL_SINGLE;
            else goto args;
        } else if(!strcmp(argv[i],"--slots")){
            if(!int_arg(argv[i+1],0,INPUT_HW_MAX_SLOTS,&slots))goto args;
        } else if(!strcmp(argv[i],"--width")){
            if(!int_arg(argv[i+1],1,4096,&width))goto args;
        } else if(!strcmp(argv[i],"--height")){
            if(!int_arg(argv[i+1],1,4096,&height))goto args;
        } else if(!strcmp(argv[i],"--x-min")){
            if(!int_arg(argv[i+1],-1000000,1000000,&xmin))goto args;
        } else if(!strcmp(argv[i],"--x-max")){
            if(!int_arg(argv[i+1],-1000000,1000000,&xmax))goto args;
        } else if(!strcmp(argv[i],"--y-min")){
            if(!int_arg(argv[i+1],-1000000,1000000,&ymin))goto args;
        } else if(!strcmp(argv[i],"--y-max")){
            if(!int_arg(argv[i+1],-1000000,1000000,&ymax))goto args;
        } else if(!strcmp(argv[i],"--swap")){
            if(!int_arg(argv[i+1],0,1,&swap))goto args;
        } else if(!strcmp(argv[i],"--invert-x")){
            if(!int_arg(argv[i+1],0,1,&ix))goto args;
        } else if(!strcmp(argv[i],"--invert-y")){
            if(!int_arg(argv[i+1],0,1,&iy))goto args;
        } else goto args;
    }
    if(!protocol||!width||!height||xmax<=xmin||ymax<=ymin)goto args;
    if(protocol==INPUT_PROTOCOL_MT_B&&slots<=0)goto args;

    InputTransform t={
        .x={xmin,xmax},.y={ymin,ymax},
        .width=(unsigned)width,.height=(unsigned)height,
        .swap_xy=swap,.invert_x=ix,.invert_y=iy
    };
    InputState state;
    if(!input_state_init(&state,protocol,(unsigned)slots,&t)){
        fprintf(stderr,"REPLAY_INIT_FAILED\n");return 1;
    }

    unsigned type,code;
    int value;
    unsigned long line=0;
    while(scanf("%u %u %d",&type,&code,&value)==3){
        ++line;
        if(type>65535||code>65535){
            fprintf(stderr,"REPLAY_EVENT_RANGE line=%lu\n",line);return 1;
        }
        int r=input_state_feed(&state,(uint16_t)type,(uint16_t)code,(int32_t)value);
        if(r==0){
            fprintf(stderr,
                    "REPLAY_EVENT_REJECTED line=%lu type=%u code=%u value=%d\n",
                    line,type,code,value);
            return 1;
        }
        if(r==3){
            fprintf(stderr,"REPLAY_RESYNC_REQUIRED line=%lu\n",line);
            return 3;
        }
        if(r==2)print_frame(input_state_frame(&state));
    }
    if(!feof(stdin)){
        fprintf(stderr,"REPLAY_PARSE_FAILED line=%lu\n",line+1);return 1;
    }
    printf("REPLAY_OK events=%lu\n",line);
    return 0;
args:
    fprintf(stderr,
            "USAGE: input-replay --protocol mtb|single --slots N --width W "
            "--height H --x-min N --x-max N --y-min N --y-max N "
            "--swap 0|1 --invert-x 0|1 --invert-y 0|1\n");
    return 2;
}
