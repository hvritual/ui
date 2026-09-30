#define _POSIX_C_SOURCE 200809L
#include "input/live.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); ++failures; goto cleanup; } } while(0)
#define PASS(x) printf("PASS %s\n",x)

int __real_open(const char *, int, ...);
int __real___open_2(const char *, int);
int __real_fstat(int, struct stat *);
int __real_ioctl(int, unsigned long, ...);
ssize_t __real_read(int, void *, size_t);
ssize_t __real___read_chk(int, void *, size_t, size_t);
int __real_poll(struct pollfd *, nfds_t, int);
int __real___poll_chk(struct pollfd *, nfds_t, int, size_t);
int __real_close(int);

static struct input_event stream_events[2048];
static size_t stream_count, stream_pos;
static int read_eof, poll_ready=1;
static int close_count, touch_node=1, poll_fault, endless_read;
static int all_absent, open_error, stat_error, raw_limit=16384, missing_mt, resync_error;
static const char *touch_name="ilitek_ts";
static int raw_y_limit, held_touch, key_state_error, slot_queries, key_queries;
static int duplicate_device, missing_tracking;
static int mt_tracking[10], mt_x[10], mt_y[10], mt_current_slot;

static void reset_fake(void) {
    memset(stream_events,0,sizeof(stream_events));
    stream_count=stream_pos=0; read_eof=0; poll_ready=1; close_count=0; touch_node=1; poll_fault=0; endless_read=0;
    for(int i=0;i<10;i++){mt_tracking[i]=-1;mt_x[i]=mt_y[i]=0;}
    mt_current_slot=0;
    raw_y_limit=held_touch=key_state_error=slot_queries=key_queries=duplicate_device=missing_tracking=0;
    all_absent=open_error=stat_error=missing_mt=resync_error=0;raw_limit=16384;touch_name="ilitek_ts";
}
static void bit_set(unsigned bit, unsigned long *bits) {
    bits[bit/(sizeof(unsigned long)*8U)] |= 1UL << (bit%(sizeof(unsigned long)*8U));
}
static void push(unsigned type,unsigned code,int value,uint64_t ns) {
    struct input_event *e=&stream_events[stream_count++];
    e->type=(uint16_t)type;e->code=(uint16_t)code;e->value=value;
    e->time.tv_sec=(time_t)(ns/1000000000ULL);
    e->time.tv_usec=(suseconds_t)((ns%1000000000ULL)/1000ULL);
}
int __wrap_open(const char *path,int flags,...) {
    (void)flags;
    if(all_absent){errno=ENOENT;return -1;}
    if(open_error && !strcmp(path,"/dev/input/event1")){errno=open_error;return -1;}
    if(!strcmp(path,"/dev/input/event0")) return 10;
    if(duplicate_device&&!strcmp(path,"/dev/input/event2"))return 11;
    if((touch_node==1&&!strcmp(path,"/dev/input/event1"))||
       (touch_node==2&&!strcmp(path,"/dev/input/event2"))) return 11;
    errno=ENOENT;return -1;
}
int __wrap___open_2(const char *path,int flags) {
    if(!strncmp(path,"/dev/input/event",16)) return __wrap_open(path,flags);
    return __real___open_2(path,flags);
}
int __wrap_fstat(int fd,struct stat *st) {
    if(fd!=10&&fd!=11)return __real_fstat(fd,st);
    if(fd==11&&stat_error){errno=stat_error;return -1;}
    memset(st,0,sizeof(*st));st->st_mode=S_IFCHR;st->st_rdev=makedev(13,(unsigned)(64+fd-10));return 0;
}
static int fill_abs(unsigned code,struct input_absinfo *a) {
    memset(a,0,sizeof(*a));
    if(code==ABS_MT_SLOT){a->minimum=0;a->maximum=9;a->value=mt_current_slot;return 1;}
    if(code==ABS_MT_TRACKING_ID){a->minimum=0;a->maximum=65535;return 1;}
    if(code==ABS_MT_POSITION_X||code==ABS_MT_POSITION_Y){a->minimum=0;a->maximum=code==ABS_MT_POSITION_Y&&raw_y_limit?raw_y_limit:raw_limit;return 1;}
    return 0;
}
int __wrap_ioctl(int fd,unsigned long request,...) {
    va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
    if(fd!=10&&fd!=11)return __real_ioctl(fd,request,arg);

    if(_IOC_NR(request)==_IOC_NR(EVIOCGNAME(256))) {
        const char *name=fd==10?"20cc000.snvs:snvs-powerkey":touch_name;
        strcpy((char *)arg,name);return (int)strlen(name)+1;
    }
    if(request==EVIOCSCLOCKID) return 0;
    if(_IOC_NR(request)==_IOC_NR(EVIOCGKEY(1))) {
        ++key_queries;
        if(key_state_error){errno=key_state_error;return -1;}
        memset(arg,0,_IOC_SIZE(request));
        if(held_touch)bit_set(BTN_TOUCH,(unsigned long *)arg);
        return 0;
    }

    unsigned nr=_IOC_NR(request);
    if(nr>=_IOC_NR(EVIOCGBIT(0,1)) && nr<=_IOC_NR(EVIOCGBIT(EV_MAX,1))) {
        unsigned ev=nr-_IOC_NR(EVIOCGBIT(0,1));
        memset(arg,0,_IOC_SIZE(request));
        if(fd==10) {
            if(ev==0) bit_set(EV_SYN,(unsigned long *)arg),bit_set(EV_KEY,(unsigned long *)arg);
            return 1;
        }
        if(ev==0){bit_set(EV_SYN,(unsigned long *)arg);bit_set(EV_KEY,(unsigned long *)arg);bit_set(EV_ABS,(unsigned long *)arg);}
        else if(ev==EV_KEY)bit_set(BTN_TOUCH,(unsigned long *)arg);
        else if(ev==EV_ABS){
            if(!missing_mt)bit_set(ABS_MT_SLOT,(unsigned long *)arg);
            if(!missing_tracking)bit_set(ABS_MT_TRACKING_ID,(unsigned long *)arg);
            bit_set(ABS_MT_POSITION_X,(unsigned long *)arg);
            bit_set(ABS_MT_POSITION_Y,(unsigned long *)arg);
        }
        return 1;
    }
    if(nr>=_IOC_NR(EVIOCGABS(0)) && nr<=_IOC_NR(EVIOCGABS(ABS_MAX))) {
        unsigned code=nr-_IOC_NR(EVIOCGABS(0));
        if(code==ABS_MT_SLOT)++slot_queries;
        if(fill_abs(code,(struct input_absinfo *)arg)) return 0;
        errno=EINVAL;return -1;
    }
    if(nr==_IOC_NR(EVIOCGMTSLOTS(4))) {
        ++slot_queries;
        if(resync_error){errno=resync_error;return -1;}
        int32_t *values=(int32_t *)arg;
        unsigned code=(unsigned)values[0];
        for(int i=0;i<10;i++) {
            if(code==ABS_MT_TRACKING_ID)values[i+1]=mt_tracking[i];
            else if(code==ABS_MT_POSITION_X)values[i+1]=mt_x[i];
            else if(code==ABS_MT_POSITION_Y)values[i+1]=mt_y[i];
            else {errno=EINVAL;return -1;}
        }
        return 0;
    }
    errno=ENOTTY;return -1;
}
ssize_t __wrap_read(int fd,void *buf,size_t count) {
    if(fd!=11)return __real_read(fd,buf,count);
    if(stream_pos<stream_count) {
        size_t available=stream_count-stream_pos;
        size_t capacity=count/sizeof(struct input_event);
        size_t n=available<capacity?available:capacity;
        memcpy(buf,&stream_events[stream_pos],n*sizeof(struct input_event));
        stream_pos+=n;return (ssize_t)(n*sizeof(struct input_event));
    }
    if(endless_read){ memset(buf,0,count);return (ssize_t)count; }
    if(read_eof)return 0;
    errno=EAGAIN;return -1;
}
ssize_t __wrap___read_chk(int fd,void *buf,size_t count,size_t buflen) {
    if(fd!=11)return __real___read_chk(fd,buf,count,buflen);
    if(count>buflen){errno=EOVERFLOW;return -1;}
    return __wrap_read(fd,buf,count);
}
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout) {
    (void)timeout;
    if(n==1&&fds[0].fd==11) {
        if(poll_fault){fds[0].revents=POLLHUP;return 1;}
        if(!poll_ready)return 0;
        fds[0].revents=POLLIN;return 1;
    }
    return __real_poll(fds,n,timeout);
}
int __wrap___poll_chk(struct pollfd *fds,nfds_t n,int timeout,size_t fdslen) {
    if(n==1&&fds&&fds[0].fd==11) {
        if(n*sizeof(*fds)>fdslen){errno=EOVERFLOW;return -1;}
        return __wrap_poll(fds,n,timeout);
    }
    return __real___poll_chk(fds,n,timeout,fdslen);
}
int __wrap_close(int fd) {
    if(fd==10||fd==11){close_count++;return 0;}
    return __real_close(fd);
}

typedef struct { InputFrame frames[32]; uint64_t ns[32]; unsigned count; } Sink;
static int sink(void *ctx,const InputFrame *frame,uint64_t ns) {
    Sink *s=(Sink *)ctx;
    if(s->count>=32)return 0;
    s->frames[s->count]=*frame;s->ns[s->count]=ns;s->count++;return 1;
}
static InputLiveConfig config(void) {
    InputLiveConfig c={.expected_name="ilitek_ts",.width=1024,.height=600,
        .swap_xy=0,.invert_x=0,.invert_y=0,.expected_raw_min=0,
        .expected_raw_max=16384,.expected_slots=10};
    return c;
}
static void discovery(void) {
    InputLive live={0};InputLiveConfig cfg=config();
    reset_fake();
    CHECK(input_live_discover(&live,"/dev/input",&cfg));
    CHECK(live.opened&&live.fd==11&&!strcmp(live.path,"/dev/input/event1")&&!strcmp(live.name,"ilitek_ts"));
    CHECK(live.kernel_monotonic&&live.state.slot_count==10&&close_count==1);
    PASS("live-discovery-skips-powerkey-selects-ilitek");
cleanup:input_live_close(&live);
}
static void normal_frames(void) {
    InputLive live={0};InputLiveConfig cfg=config();Sink s={0};
    reset_fake();CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    push(EV_KEY,BTN_TOUCH,1,1000000);
    push(EV_ABS,ABS_MT_TRACKING_ID,128,1000000);
    push(EV_ABS,ABS_MT_POSITION_X,408,1000000);
    push(EV_ABS,ABS_MT_POSITION_Y,789,1000000);
    push(EV_SYN,SYN_REPORT,0,1000000);
    push(EV_ABS,ABS_MT_POSITION_X,8192,2000000);
    push(EV_ABS,ABS_MT_POSITION_Y,8192,2000000);
    push(EV_SYN,SYN_REPORT,0,2000000);
    push(EV_ABS,ABS_MT_TRACKING_ID,-1,3000000);
    push(EV_KEY,BTN_TOUCH,0,3000000);
    push(EV_SYN,SYN_REPORT,0,3000000);
    CHECK(input_live_drain(&live,sink,&s)==3&&s.count==3);
    CHECK(s.frames[0].contact_count==1&&s.frames[0].contacts[0].id==0&&
          s.frames[0].contacts[0].x==25&&s.frames[0].contacts[0].y==29);
    CHECK(s.frames[1].contacts[0].x==512&&s.frames[1].contacts[0].y==300);
    CHECK(s.frames[2].contact_count==0&&live.frames==3&&live.events==11);
    PASS("live-drain-real-shape-tap-move-release");
cleanup:input_live_close(&live);
}
static void dropped_resync_current_slot(void) {
    InputLive live={0};InputLiveConfig cfg=config();Sink s={0};
    reset_fake();CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    push(EV_ABS,ABS_MT_SLOT,3,100);
    push(EV_ABS,ABS_MT_TRACKING_ID,33,100);
    push(EV_ABS,ABS_MT_POSITION_X,1000,100);
    push(EV_ABS,ABS_MT_POSITION_Y,2000,100);
    push(EV_SYN,SYN_REPORT,0,100);
    push(EV_SYN,SYN_DROPPED,0,200);
    push(EV_ABS,ABS_MT_POSITION_X,9999,210);
    push(EV_SYN,SYN_REPORT,0,220);
    mt_tracking[5]=55;mt_x[5]=5000;mt_y[5]=6000;mt_current_slot=5;
    CHECK(input_live_drain(&live,sink,&s)==2);
    CHECK(s.count==2&&s.frames[1].cancelled_count==1&&s.frames[1].cancelled[0]==3&&
          live.syn_dropped==1&&live.resyncs==2&&live.state.current_slot==5);
    stream_count=stream_pos=0;
    push(EV_ABS,ABS_MT_TRACKING_ID,-1,300);
    push(EV_SYN,SYN_REPORT,0,300);
    push(EV_ABS,ABS_MT_TRACKING_ID,56,400);
    push(EV_ABS,ABS_MT_POSITION_X,7000,400);
    push(EV_ABS,ABS_MT_POSITION_Y,8000,400);
    push(EV_SYN,SYN_REPORT,0,400);
    CHECK(input_live_drain(&live,sink,&s)==2);
    CHECK(s.frames[2].contact_count==0&&!s.frames[2].suppressed);
    CHECK(s.frames[3].contact_count==1&&s.frames[3].contacts[0].id==5);
    PASS("live-syn-dropped-resync-restores-current-slot");
cleanup:input_live_close(&live);
}
static void disconnect_case(void) {
    InputLive live={0};InputLiveConfig cfg=config();Sink s={0};
    reset_fake();CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    push(EV_ABS,ABS_MT_TRACKING_ID,1,1);
    push(EV_ABS,ABS_MT_POSITION_X,100,1);
    push(EV_ABS,ABS_MT_POSITION_Y,200,1);
    push(EV_SYN,SYN_REPORT,0,1);
    CHECK(input_live_drain(&live,sink,&s)==1&&s.count==1);
    read_eof=1;
    CHECK(input_live_drain(&live,sink,&s)==-1);
    CHECK(s.count==2&&s.frames[1].cancelled_count==1&&s.frames[1].cancelled[0]==0&&
          live.disconnects==1&&live.error&&!strcmp(live.error,"INPUT_DEVICE_DISCONNECTED"));
    PASS("live-disconnect-emits-terminal-cancel");
cleanup:input_live_close(&live);
}
static void wait_case(void) {
    InputLive live={0};InputLiveConfig cfg=config();
    reset_fake();CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    poll_ready=0;CHECK(input_live_wait(&live,5)==0);
    poll_ready=1;CHECK(input_live_wait(&live,5)==1);
    CHECK(input_live_wait(&live,1001)==-1);
    PASS("live-poll-bounded-no-busy-loop");
cleanup:input_live_close(&live);
}

static void startup_held(void) {
    InputLive live={0};InputLiveConfig cfg=config();Sink s={0};reset_fake();
    mt_current_slot=4;mt_tracking[4]=104;mt_x[4]=8192;mt_y[4]=8192;
    CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    CHECK(live.state.current_slot==4&&live.state.suppress_until_all_up);
    push(EV_SYN,SYN_REPORT,0,1000000);
    push(EV_ABS,ABS_MT_TRACKING_ID,-1,2000000);push(EV_SYN,SYN_REPORT,0,2000000);
    push(EV_ABS,ABS_MT_TRACKING_ID,105,3000000);push(EV_SYN,SYN_REPORT,0,3000000);
    CHECK(input_live_drain(&live,sink,&s)==3);
    CHECK(!s.frames[0].contact_count&&s.frames[0].suppressed);
    CHECK(!s.frames[1].suppressed);
    CHECK(s.frames[2].contact_count==1&&s.frames[2].contacts[0].id==4&&s.frames[2].contacts[0].x==512);
    PASS("live-open-snapshots-held-contact-until-all-up");
cleanup:input_live_close(&live);
}
static void reconnect_changed_node(void) {
    InputLive live={0};InputLiveConfig cfg=config();reset_fake();
    CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));
    live.events=17;input_live_close(&live);touch_node=2;
    CHECK(input_live_reconnect(&live,"/dev/input"));
    CHECK(live.reconnects==1&&live.reconnect_attempts==1&&live.events==17);
    CHECK(!strcmp(live.path,"/dev/input/event2")&&live.resyncs==2);
    PASS("live-reconnect-rediscovers-changed-event-node");
cleanup:input_live_close(&live);
}
static void read_budget(void) {
    InputLive live={0};InputLiveConfig cfg=config();reset_fake();
    CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));endless_read=1;
    CHECK(input_live_drain(&live,NULL,NULL)==(int)(INPUT_LIVE_READ_BUDGET*64));
    CHECK(live.budget_yields==1&&live.events==INPUT_LIVE_READ_BUDGET*64);
    PASS("live-continuous-stream-yields-to-guest-budget");
cleanup:input_live_close(&live);
}
static void hup_error(void) {
    InputLive live={0};InputLiveConfig cfg=config();reset_fake();
    CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));poll_fault=1;
    CHECK(input_live_wait(&live,1)==-1);
    CHECK(live.error&&!strcmp(live.error,"INPUT_DEVICE_DISCONNECTED"));
    PASS("live-hup-without-data-is-not-readable-spin");
cleanup:input_live_close(&live);
}

/* These call real discovery/open/validation/resync code. Only Linux syscalls
 * are substituted; unlike the framework loop fixture, discovery is not mocked. */
static void rejection_reason(const char *wanted,int system_error,const char *label) {
    InputLive live={0};InputLiveConfig cfg=config();
    CHECK(!input_live_discover(&live,"/dev/input",&cfg));
    CHECK(live.error&&!strcmp(live.error,wanted)&&live.system_errno==system_error);
    CHECK(!live.opened&&live.fd==-1);
    fprintf(stdout,"CHECKED %s\n",label);
cleanup:input_live_close(&live);
}
static void discovery_report(void) {
    InputLive live={0};InputLiveConfig cfg=config();FILE *report=NULL;
    reset_fake();raw_limit=4095;
    CHECK(!input_live_discover(&live,"/dev/input",&cfg));
    CHECK(!strcmp(live.path,"/dev/input/event1")&&!strcmp(live.name,"ilitek_ts"));
    CHECK(live.diagnostics.name_matched&&live.diagnostics.axes_queried);
    CHECK(live.diagnostics.raw_x_max==4095&&live.diagnostics.scanned==64);
    report=tmpfile();CHECK(report&&input_live_report(report,&live));rewind(report);
    char text[4096];size_t n=fread(text,1,sizeof(text)-1,report);text[n]=0;
    CHECK(strstr(text,"\"error\":\"INPUT_PROFILE_MISMATCH\"")&&strstr(text,"\"raw_x_max\":4095"));
    CHECK(strstr(text,"\"raw_max\":16384")&&strstr(text,"\"admitted\":false"));
    CHECK(fclose(report)==0);report=NULL;
    /* Untrusted device names cannot inject another JSON line. */
    strcpy(live.name,"touch\"\\\n");
    report=tmpfile();CHECK(report&&input_live_report(report,&live));rewind(report);
    n=fread(text,1,sizeof(text)-1,report);text[n]=0;
    CHECK(strstr(text,"\\\"")&&strstr(text,"\\u000a"));
    CHECK(strchr(text,'\n')==text+strlen(text)-1);
    fprintf(stdout,"CHECKED live-discovery-report-expected-observed-and-escaping\n");
cleanup:if(report)fclose(report);input_live_close(&live);
}
static void alias_config(void) {
    InputLive live={0};InputLiveConfig cfg=config();reset_fake();
    CHECK(input_live_open_path(&live,"/dev/input/event1",&cfg));input_live_close(&live);
    CHECK(input_live_open_path(&live,"/dev/input/event1",&live.config));
    CHECK(live.state.slot_count==10);input_live_close(&live);
    CHECK(input_live_discover(&live,"/dev/input",&live.config));
    fprintf(stdout,"CHECKED live-reopen-preserves-aliased-config\n");
cleanup:input_live_close(&live);
}
static void discovery_failures(void) {
    reset_fake();raw_limit=4095;
    rejection_reason("INPUT_PROFILE_MISMATCH",0,"live-discovery-preserves-profile-mismatch");
    reset_fake();resync_error=EIO;
    rejection_reason("INPUT_MT_RESYNC_FAILED",EIO,"live-discovery-preserves-resync-error");
    reset_fake();missing_mt=1;
    rejection_reason("INPUT_PROTOCOL_B_MISSING",0,"live-discovery-preserves-protocol-rejection");
    reset_fake();stat_error=EOVERFLOW;
    rejection_reason("INPUT_STAT_FAILED",EOVERFLOW,"live-discovery-preserves-stat-error");
    reset_fake();open_error=EACCES;
    rejection_reason("INPUT_DEVICE_PERMISSION",EACCES,"live-discovery-preserves-permission-error");
    reset_fake();all_absent=1;
    rejection_reason("INPUT_DEVICE_NOT_FOUND",ENOENT,"live-discovery-distinguishes-absent-nodes");
    reset_fake();touch_name="other-touch";
    rejection_reason("INPUT_NAME_MISMATCH",0,"live-discovery-preserves-name-mismatch");
}

/* Fixtures use deliberately asymmetric synthetic ranges, NOT claimed board data. */
static InputLiveConfig goodix_config(void) {
    InputLiveConfig c={.expected_name="auto",.width=1024,.height=600};
    return c;
}
static void goodix_reset(void) {
    reset_fake();touch_name="goodix-ts";missing_mt=1;raw_limit=1023;raw_y_limit=599;
}
static void packet(int id,int x,int y) {
    push(EV_ABS,ABS_MT_TRACKING_ID,id,1000000);
    push(EV_ABS,ABS_MT_POSITION_X,x,1000000);
    push(EV_ABS,ABS_MT_POSITION_Y,y,1000000);
    push(EV_SYN,SYN_MT_REPORT,0,1000000);
}
static void report_a(int down) {
    push(EV_KEY,BTN_TOUCH,down,1000000);
    push(EV_SYN,SYN_REPORT,0,1000000);
}
static void goodix_admission(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();FILE *report=NULL;
    goodix_reset();CHECK(input_live_discover(&l,"/dev/input",&c));
    CHECK(!strcmp(l.name,"goodix-ts")&&l.state.protocol==INPUT_PROTOCOL_MT_A);
    CHECK(l.state.transform.x.maximum==1023&&l.state.transform.y.maximum==599);
    CHECK(!slot_queries&&key_queries==1);
    report=tmpfile();CHECK(report&&input_live_report(report,&l));rewind(report);
    char text[4096];size_t n=fread(text,1,sizeof(text)-1,report);text[n]=0;
    CHECK(strstr(text,"\"protocol\":\"mt-a\"")&&strstr(text,"\"axis_source\":\"kernel-probe\""));
    CHECK(strstr(text,"\"slot_range_present\":false")&&strstr(text,"\"raw_y_max\":599"));
    puts("CHECKED goodix-admission-queries-independent-axes-without-slots");
cleanup:if(report)fclose(report);input_live_close(&l);
}
static void goodix_frames(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();Sink out={0};
    goodix_reset();CHECK(input_live_discover(&l,"/dev/input",&c));
    packet(0,0,0);packet(7,1023,599);report_a(1);
    packet(7,900,500);packet(0,200,100);report_a(1); /* reverse packet order */
    packet(7,800,400);report_a(1); /* release ID zero only */
    push(EV_SYN,SYN_MT_REPORT,0,1000000);report_a(0);
    CHECK(input_live_drain(&l,sink,&out)==4&&out.count==4);
    CHECK(out.frames[0].contact_count==2&&out.frames[0].contacts[0].id==0);
    CHECK(out.frames[0].contacts[1].x==1023&&out.frames[0].contacts[1].y==599);
    CHECK(out.frames[1].contacts[0].x==200&&out.frames[1].contacts[1].x==900);
    CHECK(out.frames[2].contact_count==1&&out.frames[2].contacts[0].id==1);
    CHECK(!out.frames[3].contact_count&&!out.frames[3].suppressed&&!slot_queries);
    puts("CHECKED goodix-raw-packets-zero-id-reorder-move-release");
cleanup:input_live_close(&l);
}
static void goodix_held_drop(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();Sink out={0};
    goodix_reset();held_touch=1;CHECK(input_live_discover(&l,"/dev/input",&c));
    packet(0,300,200);report_a(1);report_a(0);
    packet(0,300,200);report_a(1);
    CHECK(input_live_drain(&l,sink,&out)==3);
    CHECK(out.frames[0].suppressed&&!out.frames[0].contact_count);
    CHECK(!out.frames[1].suppressed&&out.frames[2].contact_count==1);
    push(EV_SYN,SYN_DROPPED,0,2000000);
    packet(3,500,400);report_a(1); /* ignored to the report, then query held key */
    packet(3,500,400);report_a(1);report_a(0);
    packet(0,100,100);report_a(1);
    CHECK(input_live_drain(&l,sink,&out)==4&&out.count==7);
    CHECK(out.frames[3].syn_dropped&&out.frames[3].cancelled_count==1);
    CHECK(out.frames[4].suppressed&&!out.frames[4].contact_count);
    CHECK(!out.frames[5].suppressed&&out.frames[6].contact_count==1);
    CHECK(key_queries==2&&!slot_queries&&l.syn_dropped==1);
    puts("CHECKED goodix-held-start-syn-dropped-query-key-suppress-until-release");
cleanup:input_live_close(&l);
}
static void goodix_bad_frames(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();Sink out={0};
    goodix_reset();CHECK(input_live_discover(&l,"/dev/input",&c));
    packet(0,100,100);report_a(1);CHECK(input_live_drain(&l,sink,&out)==1);
    packet(1,200,100);packet(1,250,100);report_a(1);
    CHECK(input_live_drain(&l,sink,&out)==-1);
    CHECK(out.frames[out.count-1].suppressed&&out.frames[out.count-1].cancelled_count==1);
    input_live_close(&l);goodix_reset();out=(Sink){0};
    CHECK(input_live_discover(&l,"/dev/input",&c));
    push(EV_ABS,ABS_MT_TRACKING_ID,0,1000000);
    push(EV_ABS,ABS_MT_POSITION_X,100,1000000);
    push(EV_SYN,SYN_MT_REPORT,0,1000000);report_a(1);
    CHECK(input_live_drain(&l,sink,&out)==-1);CHECK(!out.frames[0].contact_count);
    puts("CHECKED goodix-duplicate-id-incomplete-packet-cancel-not-click");
cleanup:input_live_close(&l);
}
static void goodix_overflow(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();Sink out={0};
    goodix_reset();CHECK(input_live_discover(&l,"/dev/input",&c));
    packet(0,100,100);report_a(1);
    for(int i=0;i<9;i++)packet(i,100+i,100);
    report_a(1);packet(0,100,100);report_a(1);report_a(0);
    packet(0,100,100);report_a(1);
    CHECK(input_live_drain(&l,sink,&out)==5&&out.count==5);
    CHECK(out.frames[1].suppressed&&out.frames[1].cancelled_count==1);
    CHECK(out.frames[2].suppressed&&!out.frames[2].contact_count);
    CHECK(!out.frames[3].suppressed&&out.frames[4].contact_count==1);
    puts("CHECKED goodix-contact-overflow-bounded-cancel-recovery");
cleanup:input_live_close(&l);
}
static void goodix_reconnect(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();Sink out={0};
    goodix_reset();CHECK(input_live_discover(&l,"/dev/input",&c));
    packet(0,100,100);report_a(1);CHECK(input_live_drain(&l,sink,&out)==1);
    read_eof=1;CHECK(input_live_drain(&l,sink,&out)==-1);input_live_close(&l);
    touch_node=2;read_eof=0;held_touch=1;
    CHECK(input_live_reconnect(&l,"/dev/input"));CHECK(l.state.suppress_until_all_up);
    CHECK(!strcmp(l.path,"/dev/input/event2"));input_live_close(&l);
    raw_y_limit=799;CHECK(!input_live_reconnect(&l,"/dev/input"));
    CHECK(!l.opened&&!strcmp(l.error,"INPUT_RECONNECT_PROFILE_CHANGED"));
    puts("CHECKED goodix-reconnect-new-event-index-reject-profile-drift");
cleanup:input_live_close(&l);
}
static void goodix_rejections(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();
    goodix_reset();missing_tracking=1;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_MT_CAPABILITIES_MISSING"));
    goodix_reset();raw_limit=0;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_AXIS_RANGE_INVALID"));
    goodix_reset();key_state_error=EIO;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_KEY_STATE_QUERY_FAILED"));
    goodix_reset();c.expected_slots=10;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_SLOT_PROFILE_INAPPLICABLE"));
    c=goodix_config();c.expected_raw_max=16384;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_PROFILE_MISMATCH"));
    c=goodix_config();goodix_reset();duplicate_device=1;
    CHECK(!input_live_discover(&l,"/dev/input",&c));CHECK(!strcmp(l.error,"INPUT_DEVICE_AMBIGUOUS"));
    CHECK(!l.opened);
    puts("CHECKED goodix-reject-missing-id-bad-range-key-query-legacy-slots-ambiguity");
cleanup:input_live_close(&l);
}
static void auto_ilitek(void) {
    InputLive l={0};InputLiveConfig c=goodix_config();reset_fake();
    CHECK(input_live_discover(&l,"/dev/input",&c));
    CHECK(l.state.protocol==INPUT_PROTOCOL_MT_B&&l.state.slot_count==10);
    CHECK(l.state.transform.x.maximum==16384&&slot_queries==5&&!key_queries);
    puts("CHECKED auto-ilitek-preserves-historical-b-ranges-resync");
cleanup:input_live_close(&l);
}

int main(void) {
    goodix_admission();goodix_frames();goodix_held_drop();goodix_bad_frames();
    goodix_overflow();goodix_reconnect();goodix_rejections();auto_ilitek();
    if(!failures)puts("INPUT_MT_A_OK groups=8");
    discovery_failures();discovery_report();alias_config();
    startup_held();reconnect_changed_node();read_budget();hup_error();discovery();normal_frames();dropped_resync_current_slot();disconnect_case();wait_case();
    if(failures){fprintf(stderr,"INPUT_LIVE_FAILED failures=%d\n",failures);return 1;}
    printf("INPUT_LIVE_OK cases=9\n");return 0;
}
