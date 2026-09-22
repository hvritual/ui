#define _POSIX_C_SOURCE 200809L
#include "fake_input.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <poll.h>

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define SET_BIT(bit,array) ((array)[(bit)/BITS_PER_LONG] |= 1UL << ((bit)%BITS_PER_LONG))

int __real_open(const char *, int, ...);
int __real___open_2(const char *, int);
int __real_fstat(int, struct stat *);
int __real_ioctl(int, unsigned long, ...);
ssize_t __real_read(int, void *, size_t);
int __real_poll(struct pollfd *, nfds_t, int);
int __real_close(int);

FakeInput fake_input;

void fake_input_reset(void) {
    memset(&fake_input, 0, sizeof(fake_input));
    fake_input.current_slot = 0;
    for (int i=0;i<10;i++) fake_input.tracking[i] = -1;
}
void fake_input_push(unsigned type, unsigned code, int value, long sec, long usec) {
    if (fake_input.event_count >= FAKE_INPUT_MAX_EVENTS) return;
    struct input_event *e=&fake_input.events[fake_input.event_count++];
    memset(e,0,sizeof(*e));
    e->time.tv_sec=sec; e->time.tv_usec=usec;
    e->type=(__u16)type; e->code=(__u16)code; e->value=value;
}
static int fixture_fd(int fd) { return fd==40 || fd==41; }

int __wrap_open(const char *path,int flags,...) {
    if (!strcmp(path,"/fixture/event0")) { fake_input.opens++; return 40; }
    if (!strcmp(path,"/fixture/event1")) {
        fake_input.opens++; fake_input.selected_flags=flags; return 41;
    }
    if (!strncmp(path,"/fixture/event",14)) { errno=ENOENT; return -1; }
    if (flags & O_CREAT) {
        va_list ap; va_start(ap,flags); mode_t mode=(mode_t)va_arg(ap,int); va_end(ap);
        return __real_open(path,flags,mode);
    }
    return __real_open(path,flags);
}
int __wrap___open_2(const char *path,int flags) {
    if (!strncmp(path,"/fixture/event",14)) return __wrap_open(path,flags);
    return __real___open_2(path,flags);
}
int __wrap_fstat(int fd,struct stat *st) {
    if (!fixture_fd(fd)) return __real_fstat(fd,st);
    memset(st,0,sizeof(*st)); st->st_mode=S_IFCHR; st->st_rdev=makedev(13,fd==40?64:65); return 0;
}
static void ev_bits(void *arg,size_t bytes,int touch) {
    memset(arg,0,bytes); unsigned long *b=arg; SET_BIT(EV_KEY,b); if(touch) SET_BIT(EV_ABS,b);
}
static void key_bits(void *arg,size_t bytes,int touch) {
    memset(arg,0,bytes); if(touch){unsigned long *b=arg;SET_BIT(BTN_TOUCH,b);}
}
static void abs_bits(void *arg,size_t bytes) {
    memset(arg,0,bytes); unsigned long *b=arg;
    SET_BIT(ABS_X,b);SET_BIT(ABS_Y,b);SET_BIT(ABS_MT_SLOT,b);SET_BIT(ABS_MT_TRACKING_ID,b);
    SET_BIT(ABS_MT_POSITION_X,b);SET_BIT(ABS_MT_POSITION_Y,b);
}
static int abs_info(unsigned long request, void *arg) {
    struct input_absinfo *a=arg; memset(a,0,sizeof(*a));
    if(request==EVIOCGABS(ABS_MT_SLOT)){a->minimum=0;a->maximum=9;a->value=fake_input.current_slot;return 1;}
    if(request==EVIOCGABS(ABS_MT_TRACKING_ID)){a->minimum=0;a->maximum=65535;return 1;}
    if(request==EVIOCGABS(ABS_MT_POSITION_X)||request==EVIOCGABS(ABS_X)){a->minimum=0;a->maximum=16384;return 1;}
    if(request==EVIOCGABS(ABS_MT_POSITION_Y)||request==EVIOCGABS(ABS_Y)){a->minimum=0;a->maximum=16384;return 1;}
    return 0;
}
int __wrap_ioctl(int fd,unsigned long request,...) {
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void*); va_end(ap);
    if (!fixture_fd(fd)) return __real_ioctl(fd,request,arg);
    fake_input.ioctls++;
    if(request==EVIOCGNAME(256)){
        const char *name=fd==40?"20cc000.snvs:snvs-powerkey":"ilitek_ts";
        strcpy(arg,name); return (int)strlen(name)+1;
    }
    if(request==EVIOCGBIT(0,sizeof(unsigned long)*(((EV_MAX+1-1)/(sizeof(unsigned long)*8))+1))){
        ev_bits(arg,_IOC_SIZE(request),fd==41); return 0;
    }
    if(fd==41 && request==EVIOCGBIT(EV_KEY,sizeof(unsigned long)*(((KEY_MAX+1-1)/(sizeof(unsigned long)*8))+1))){
        key_bits(arg,_IOC_SIZE(request),1); return 0;
    }
    if(fd==41 && request==EVIOCGBIT(EV_ABS,sizeof(unsigned long)*(((ABS_MAX+1-1)/(sizeof(unsigned long)*8))+1))){
        abs_bits(arg,_IOC_SIZE(request)); return 0;
    }
    if(fd==41 && abs_info(request,arg)) return 0;
#ifdef EVIOCSCLOCKID
    if(fd==41 && request==EVIOCSCLOCKID) return 0;
#endif
    if(fd==41 && _IOC_NR(request)==0x0a && _IOC_TYPE(request)=='E') {
        int32_t *values=arg; int code=values[0];
        for(int i=0;i<10;i++) {
            if(code==ABS_MT_TRACKING_ID) values[i+1]=fake_input.tracking[i];
            else if(code==ABS_MT_POSITION_X) values[i+1]=fake_input.x[i];
            else if(code==ABS_MT_POSITION_Y) values[i+1]=fake_input.y[i];
            else { errno=EINVAL; return -1; }
        }
        return 0;
    }
    fake_input.forbidden++; errno=ENOTTY; return -1;
}
ssize_t __wrap_read(int fd,void *buf,size_t count) {
    if(fd!=41) return __real_read(fd,buf,count);
    fake_input.reads++;
    if(fake_input.event_index<fake_input.event_count){
        size_t left=fake_input.event_count-fake_input.event_index;
        size_t cap=count/sizeof(struct input_event);
        size_t n=left<cap?left:cap;
        memcpy(buf,&fake_input.events[fake_input.event_index],n*sizeof(struct input_event));
        fake_input.event_index+=n;
        return (ssize_t)(n*sizeof(struct input_event));
    }
    if(fake_input.read_zero) return 0;
    errno=EAGAIN; return -1;
}
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout) {
    if(n==1 && fds[0].fd==41){
        (void)timeout; fake_input.polls++; fds[0].revents=0;
        if(fake_input.poll_hup){fds[0].revents=POLLHUP;return 1;}
        if(fake_input.event_index<fake_input.event_count){fds[0].revents=POLLIN;return 1;}
        return 0;
    }
    return __real_poll(fds,n,timeout);
}
int __wrap_close(int fd) {
    if(fixture_fd(fd)){fake_input.closes++;return 0;}
    return __real_close(fd);
}
