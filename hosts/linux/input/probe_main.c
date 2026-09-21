#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define NBITS(x) ((((x) - 1U) / BITS_PER_LONG) + 1U)
#define TEST_BIT(bit, array) (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1UL)

typedef struct { int present; struct input_absinfo info; } Axis;
typedef struct {
    int observed, error_no, version;
    const char *error;
    struct input_id id;
    char name[256];
    unsigned long ev[NBITS(EV_MAX + 1)], key[NBITS(KEY_MAX + 1)], abs[NBITS(ABS_MAX + 1)];
    Axis abs_x, abs_y, mt_slot, mt_tracking, mt_x, mt_y;
} Probe;

static int query_bits(int fd, unsigned type, unsigned long *bits, size_t bytes) {
    memset(bits, 0, bytes);
    return ioctl(fd, EVIOCGBIT(type, bytes), bits) >= 0;
}
static void query_axis(int fd, int code, Axis *axis) {
    if (ioctl(fd, EVIOCGABS(code), &axis->info) == 0) axis->present = 1;
}
static void json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (size_t i=0; s && s[i]; ++i) {
        unsigned char c=(unsigned char)s[i];
        if (c=='"' || c=='\\') { fputc('\\',out); fputc(c,out); }
        else if (c<32 || c>=127) fprintf(out,"\\u%04x",c);
        else fputc(c,out);
    }
    fputc('"',out);
}
static void bits_hex(FILE *out, const unsigned long *bits, size_t words) {
    fputc('"', out);
    const unsigned char *p=(const unsigned char *)bits;
    for(size_t i=0;i<words*sizeof(unsigned long);++i) fprintf(out,"%02x",p[i]);
    fputc('"', out);
}
static void axis_json(FILE *out, const Axis *a) {
    if (!a->present) { fputs("null",out); return; }
    fprintf(out,"{\"min\":%d,\"max\":%d,\"fuzz\":%d,\"flat\":%d,\"resolution\":%d}",
            a->info.minimum,a->info.maximum,a->info.fuzz,a->info.flat,a->info.resolution);
}
static int probe_path(const char *path, Probe *p) {
    memset(p,0,sizeof(*p));
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0){p->error="INPUT_OPEN_FAILED";p->error_no=errno;return 0;}
    struct stat st;
    if(fstat(fd,&st)){p->error="INPUT_STAT_FAILED";p->error_no=errno;close(fd);return 0;}
    if(!S_ISCHR(st.st_mode) || major(st.st_rdev)!=13){p->error="INPUT_NOT_EVDEV";close(fd);return 0;}
    if(ioctl(fd,EVIOCGVERSION,&p->version)<0 || ioctl(fd,EVIOCGID,&p->id)<0){p->error="INPUT_QUERY_FAILED";p->error_no=errno;close(fd);return 0;}
    if(ioctl(fd,EVIOCGNAME(sizeof(p->name)),p->name)<0) p->name[0]='\0';
    if(!query_bits(fd,0,p->ev,sizeof(p->ev))){p->error="INPUT_CAPABILITY_FAILED";p->error_no=errno;close(fd);return 0;}
    if(TEST_BIT(EV_KEY,p->ev) && !query_bits(fd,EV_KEY,p->key,sizeof(p->key))){p->error="INPUT_KEY_CAPABILITY_FAILED";p->error_no=errno;close(fd);return 0;}
    if(TEST_BIT(EV_ABS,p->ev) && !query_bits(fd,EV_ABS,p->abs,sizeof(p->abs))){p->error="INPUT_ABS_CAPABILITY_FAILED";p->error_no=errno;close(fd);return 0;}
    if(TEST_BIT(EV_ABS,p->ev)){
        if(TEST_BIT(ABS_X,p->abs)) query_axis(fd,ABS_X,&p->abs_x);
        if(TEST_BIT(ABS_Y,p->abs)) query_axis(fd,ABS_Y,&p->abs_y);
        if(TEST_BIT(ABS_MT_SLOT,p->abs)) query_axis(fd,ABS_MT_SLOT,&p->mt_slot);
        if(TEST_BIT(ABS_MT_TRACKING_ID,p->abs)) query_axis(fd,ABS_MT_TRACKING_ID,&p->mt_tracking);
        if(TEST_BIT(ABS_MT_POSITION_X,p->abs)) query_axis(fd,ABS_MT_POSITION_X,&p->mt_x);
        if(TEST_BIT(ABS_MT_POSITION_Y,p->abs)) query_axis(fd,ABS_MT_POSITION_Y,&p->mt_y);
    }
    p->observed=1; close(fd); return 1;
}
static void report(FILE *out,const char *path,const Probe *p){
    int ev_key=p->observed && TEST_BIT(EV_KEY,p->ev), ev_abs=p->observed && TEST_BIT(EV_ABS,p->ev);
    int btn_touch=ev_key && TEST_BIT(BTN_TOUCH,p->key);
    int single=ev_abs && p->abs_x.present && p->abs_y.present && btn_touch;
    int mtb=ev_abs && p->mt_slot.present && p->mt_tracking.present && p->mt_x.present && p->mt_y.present;
    fprintf(out,"{\"schema_version\":1,\"operation\":\"evdev-capability-probe\",\"device\":");json_string(out,path);
    fprintf(out,",\"observed\":%s,\"error\":",p->observed?"true":"false"); if(p->error)json_string(out,p->error);else fputs("null",out);
    fprintf(out,",\"errno\":%d,\"read_only\":true,\"name\":",p->error_no);json_string(out,p->name);
    fprintf(out,",\"input_version\":%d,\"id\":{\"bustype\":%u,\"vendor\":%u,\"product\":%u,\"version\":%u}",p->version,p->id.bustype,p->id.vendor,p->id.product,p->id.version);
    fprintf(out,",\"capabilities\":{\"ev_key\":%s,\"ev_abs\":%s,\"btn_touch\":%s,\"single_touch_candidate\":%s,\"mt_protocol_b_candidate\":%s,\"ev_bits_hex\":",ev_key?"true":"false",ev_abs?"true":"false",btn_touch?"true":"false",single?"true":"false",mtb?"true":"false");
    bits_hex(out,p->ev,NBITS(EV_MAX+1)); fputs(",\"key_bits_hex\":",out); bits_hex(out,p->key,NBITS(KEY_MAX+1)); fputs(",\"abs_bits_hex\":",out); bits_hex(out,p->abs,NBITS(ABS_MAX+1)); fputs("}",out);
    fputs(",\"axes\":{\"abs_x\":",out);axis_json(out,&p->abs_x);fputs(",\"abs_y\":",out);axis_json(out,&p->abs_y);fputs(",\"mt_slot\":",out);axis_json(out,&p->mt_slot);fputs(",\"mt_tracking_id\":",out);axis_json(out,&p->mt_tracking);fputs(",\"mt_position_x\":",out);axis_json(out,&p->mt_x);fputs(",\"mt_position_y\":",out);axis_json(out,&p->mt_y);fputs("}}\n",out);
}
int main(int argc,char **argv){
    if(argc!=3 || strcmp(argv[1],"--probe")){fprintf(stderr,"USAGE: input-probe --probe /dev/input/eventN\n");return 2;}
    Probe p; int ok=probe_path(argv[2],&p); report(stdout,argv[2],&p);
    fprintf(stderr,"INPUT_PROBE_%s device=%s error=%s errno=%d\n",ok?"OK":"FAILED",argv[2],p.error?p.error:"none",p.error_no);
    return ok?0:1;
}
