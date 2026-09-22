#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop_now(int sig) { stopped = sig; }

static int uint_arg(const char *s, unsigned min, unsigned max, unsigned *out) {
    if (!s || !*s) return 0;
    for (const char *p=s; *p; ++p) if (*p<'0'||*p>'9') return 0;
    errno=0; char *end=NULL; unsigned long v=strtoul(s,&end,10);
    if (errno || !end || *end || v<min || v>max) return 0;
    *out=(unsigned)v; return 1;
}
static int valid_hash(const char *s) {
    if (!s || strlen(s)!=64) return 0;
    for (size_t i=0;i<64;i++)
        if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return 1;
}
static void json_string(FILE *out,const char *s) {
    fputc('"',out);
    for(size_t i=0;s&&s[i];++i){
        unsigned char c=(unsigned char)s[i];
        if(c=='"'||c=='\\'){fputc('\\',out);fputc(c,out);}
        else if(c<32||c>=127)fprintf(out,"\\u%04x",c);
        else fputc(c,out);
    }
    fputc('"',out);
}
static uint64_t mono_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC,&ts)) return 0;
    return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static int remaining_ms(uint64_t deadline) {
    uint64_t now=mono_ns();
    if (!now || now>=deadline) return 0;
    uint64_t ns=deadline-now;
    uint64_t ms=(ns+999999ULL)/1000000ULL;
    return ms>1000ULL?1000:(int)ms;
}
static int open_evdev(const char *path) {
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return -1;
    struct stat st;
    if(fstat(fd,&st)||!S_ISCHR(st.st_mode)||major(st.st_rdev)!=13){
        int e=errno?errno:ENODEV;close(fd);errno=e;return -1;
    }
    return fd;
}
int main(int argc,char **argv) {
    const char *path=NULL,*name=NULL,*hash=NULL,*output=NULL;
    unsigned duration_ms=15000,max_events=20000,seen=0;
    for(int i=1;i<argc;i+=2){
        if(i+1>=argc) goto args;
        if(!strcmp(argv[i],"--capture")) path=argv[i+1];
        else if(!strcmp(argv[i],"--name")) name=argv[i+1];
        else if(!strcmp(argv[i],"--capability-hash")) hash=argv[i+1];
        else if(!strcmp(argv[i],"--duration-ms")) {
            if(!uint_arg(argv[i+1],1000,30000,&duration_ms))goto args;
        } else if(!strcmp(argv[i],"--max-events")) {
            if(!uint_arg(argv[i+1],1,100000,&max_events))goto args;
        } else if(!strcmp(argv[i],"--output")) output=argv[i+1];
        else goto args;
    }
    if(!path||!name||!*name||!valid_hash(hash)||!output||!*output) goto args;

    int fd=open_evdev(path);
    if(fd<0){fprintf(stderr,"INPUT_TRACE_OPEN_FAILED errno=%d\n",errno);return 1;}
    int outfd=open(output,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(outfd<0){
        int e=errno;close(fd);
        fprintf(stderr,"INPUT_TRACE_OUTPUT_FAILED errno=%d\n",e);return 1;
    }
    FILE *out=fdopen(outfd,"w");
    if(!out){
        int e=errno;close(outfd);unlink(output);close(fd);
        fprintf(stderr,"INPUT_TRACE_OUTPUT_FAILED errno=%d\n",e);return 1;
    }

#ifdef EVIOCSCLOCKID
    int clock_id=CLOCK_MONOTONIC;
    int kernel_monotonic=ioctl(fd,EVIOCSCLOCKID,&clock_id)==0;
#else
    int kernel_monotonic=0;
#endif
    struct sigaction sa={0},old_int={0},old_term={0};
    sa.sa_handler=stop_now; sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT,&sa,&old_int)||sigaction(SIGTERM,&sa,&old_term)){
        int e=errno;fclose(out);unlink(output);close(fd);
        fprintf(stderr,"INPUT_TRACE_SIGNAL_FAILED errno=%d\n",e);return 1;
    }

    fprintf(out,"{\"schema_version\":1,\"source\":\"real-device-redacted\",\"device\":{\"name\":");
    json_string(out,name);fputs(",\"event_path\":",out);json_string(out,path);
    fputs(",\"capability_hash\":",out);json_string(out,hash);
    fprintf(out,"},\"events\":[");

    uint64_t start=mono_ns();
    uint64_t deadline=start+(uint64_t)duration_ms*1000000ULL;
    int first=1,error=0;
    while(!stopped && seen<max_events){
        int wait=remaining_ms(deadline);
        if(wait<=0)break;
        struct pollfd pfd={.fd=fd,.events=POLLIN};
        int pr;
        do{pr=poll(&pfd,1,wait);}while(pr<0&&errno==EINTR&&!stopped);
        if(stopped)break;
        if(pr<0){error=errno;break;}
        if(pr==0)continue;
        if(pfd.revents&(POLLERR|POLLHUP|POLLNVAL)){error=EIO;break;}
        if(!(pfd.revents&POLLIN))continue;

        struct input_event events[64];
        ssize_t n=read(fd,events,sizeof(events));
        if(n<0&&errno==EAGAIN)continue;
        if(n<0&&errno==EINTR)continue;
        if(n<=0){error=n==0?ENODEV:errno;break;}
        if((size_t)n%sizeof(events[0])){error=EPROTO;break;}
        size_t count=(size_t)n/sizeof(events[0]);
        for(size_t i=0;i<count&&seen<max_events;i++){
            uint64_t ns;
            if(kernel_monotonic)
                ns=(uint64_t)events[i].time.tv_sec*1000000000ULL+
                   (uint64_t)events[i].time.tv_usec*1000ULL;
            else ns=mono_ns();
            if(!first) fputc(',',out);
            first=0;
            fprintf(out,"{\"seq\":%u,\"monotonic_ns\":%llu,\"type\":%u,\"code\":%u,\"value\":%d}",
                    seen,(unsigned long long)ns,(unsigned)events[i].type,
                    (unsigned)events[i].code,events[i].value);
            ++seen;
        }
    }
    fputs("]}\n",out);
    int write_error=ferror(out)||fflush(out)||fclose(out);
    sigaction(SIGINT,&old_int,NULL);
    sigaction(SIGTERM,&old_term,NULL);
    close(fd);

    if(error||write_error||seen==0){
        fprintf(stderr,
                "INPUT_TRACE_FAILED events=%u errno=%d write_error=%d timestamp=%s\n",
                seen,error,write_error,
                kernel_monotonic?"kernel-monotonic":"userspace-monotonic");
        if(error||write_error)unlink(output);
        return 1;
    }
    fprintf(stderr,
            "INPUT_TRACE_OK events=%u duration_ms=%u timestamp=%s interrupted=%d\n",
            seen,duration_ms,
            kernel_monotonic?"kernel-monotonic":"userspace-monotonic",
            (int)stopped);
    return 0;
args:
    fprintf(stderr,
            "USAGE: input-trace --capture /dev/input/eventN --name NAME "
            "--capability-hash 64hex --duration-ms 1000..30000 "
            "--max-events 1..100000 --output NEW.json\n");
    return 2;
}
