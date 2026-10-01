#define _POSIX_C_SOURCE 200809L
#include "replay.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int parse(const char *line, long long values[5]) {
    for (unsigned i=0;i<5;i++) {
        while (isspace((unsigned char)*line)) line++;
        if (!*line) return 0;
        char *end; errno=0; values[i]=strtoll(line,&end,10);
        if (errno || end==line || (*end && !isspace((unsigned char)*end))) return 0;
        line=end;
    }
    while (isspace((unsigned char)*line)) line++;
    return !*line && values[0]>=0 && values[0]<=60000 && values[1]>=0 && values[1]<=4 &&
        values[2]>=0 && values[2]<8 && values[3]>=-65536 && values[3]<=65536 &&
        values[4]>=-65536 && values[4]<=65536;
}
int pocket_framework_replay(PocketFramework *r,const char *path,uint64_t start,unsigned *samples) {
    if (!r || !r->opened || !path || !samples || r->display.present || start>UINT64_MAX-60000000000ULL) return 0;
    *samples=0;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return 0;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<1 || st.st_size>1024*1024) {close(fd);return 0;}
    FILE *file=fdopen(fd,"r");if(!file){close(fd);return 0;}
    char line[160]; unsigned active[8]={0}; int xs[8]={0},ys[8]={0};
    uint64_t previous=0,sequence=0; HostClock clock;host_clock_start(&clock,start);
    int ok=1;
    while (ok && fgets(line,sizeof(line),file)) {
        long long v[5];
        if (++*samples>10000 || !strchr(line,'\n') || !parse(line,v) || (uint64_t)v[0]<previous) {ok=0;break;}
        previous=(uint64_t)v[0]; uint64_t now=start+previous*1000000ULL;
        while (clock.next_ns<=now) {
            uint64_t tick=clock.next_ns;int due=host_clock_due(&clock,tick);
            if (due<1 || due>4) {ok=0;break;}
            for(int i=0;i<due;i++) if(!pocket_framework_tick(r,tick,0)){ok=0;break;}
            if(!ok)break;
        }
        if(!ok)break;
        unsigned phase=(unsigned)v[1],id=(unsigned)v[2];
        if(!phase) {
            if(id || v[3] || v[4] || !pocket_framework_tick(r,now,1))ok=0;
            continue;
        }
        if(phase==1) {if(active[id]){ok=0;break;}active[id]=1;}
        else if(phase==2 || phase==3) {if(!active[id]){ok=0;break;}if(phase==3)active[id]=0;}
        else memset(active,0,sizeof(active));
        xs[id]=(int)v[3];ys[id]=(int)v[4];
        InputFrame frame={.sequence=++sequence};
        if(phase==4){frame.syn_dropped=1;frame.suppressed=1;}
        for(unsigned i=0;i<8;i++) if(active[i])frame.contacts[frame.contact_count++]=(InputContact){i,xs[i],ys[i]};
        if(!pocket_framework_input(r,&frame,now) || !pocket_framework_tick(r,now,1))ok=0;
    }
    if(ferror(file) || !*samples)ok=0;
    if(fclose(file))ok=0;
    return ok;
}
