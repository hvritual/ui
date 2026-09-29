#define _POSIX_C_SOURCE 200809L
#define POCKET_TEST_SYNTHETIC_IO 1
#define main framework_program_main
#include "../../hosts/linux/framework_main.c"
#undef main

/* Exercise the real production CLI/main loop with only device I/O and time
 * replaced. Renderer, app, F6 bridge, navigation and cleanup remain real.
 * Its report is explicitly synthetic, never admissible as physical evidence. */
static uint64_t virtual_ns=1000000000ULL;
static unsigned unblanks,flushes,reconnections,delivered_index;
static int hup_sent,invalid_frame;
static unsigned viewport_height, discovery_calls;
static unsigned startup_failures;
static const char *startup_error;
static int startup_errno;
static const unsigned event_ms[]={50,100,150,200,250,300,1300,1350,1400,1450,1600,1650,1700,1750,1800,1850,7200,7250};
int __wrap_host_monotonic_ns(uint64_t *out){virtual_ns+=100000ULL;*out=virtual_ns;return 1;}
int __wrap_host_sleep_until(uint64_t when){if(when>virtual_ns)virtual_ns=when;return 1;}
int __wrap_fbdev_open(FbDevice *d,const char *path,int writable){
    (void)path;memset(d,0,sizeof(*d));d->fd=90;d->opened=1;d->writable=writable;
    d->layout.width=1024;d->layout.height=viewport_height;return 1;
}
int __wrap_fbdev_close(FbDevice *d){d->opened=0;return 1;}
int __wrap_fbdev_report(FILE *out,const char *path,const FbDevice *d){
    (void)path;(void)d;return fprintf(out,"{\"synthetic\":true}\n")>0;
}
int __wrap_fbdev_present(void *p,const HostFrame *f){
    FbDevice *d=p;
    if(!f||!f->pixels||f->width!=1024||f->height!=viewport_height||f->stride!=4096||!unblanks){invalid_frame=1;return 0;}
    d->presents++;d->bytes_written+=f->length;flushes++;return 1;
}
int __wrap_ioctl(int fd,unsigned long request,...){
    if(fd==90&&request==FBIOBLANK){unblanks++;return 0;}
    errno=ENOTTY;return -1;
}
int __wrap_input_live_discover(InputLive *l,const char *dir,const InputLiveConfig *cfg){
    (void)dir;memset(l,0,sizeof(*l));l->fd=-1;l->config=*cfg;
    ++discovery_calls;
    if(discovery_calls<=startup_failures){l->error=startup_error;l->system_errno=startup_errno;return 0;}
    l->opened=1;l->fd=91;
    InputTransform t={.x={0,16384},.y={0,16384},.width=1024,.height=viewport_height};
    return input_state_init(&l->state,INPUT_PROTOCOL_MT_B,10,&t);
}
int __wrap_input_live_wait(InputLive *l,int ms){
    (void)l;if(ms>0)virtual_ns+=(uint64_t)ms*1000000ULL;
    if(!hup_sent&&virtual_ns>=1500000000ULL){hup_sent=1;return -1;}
    return delivered_index<sizeof(event_ms)/sizeof(event_ms[0])&&virtual_ns>=1000000000ULL+(uint64_t)event_ms[delivered_index]*1000000ULL;
}
void __wrap_input_live_close(InputLive *l){l->opened=0;l->fd=-1;}
int __wrap_input_live_reconnect(InputLive *l,const char *dir){
    (void)dir;l->opened=1;l->fd=91;l->state.disconnected=0;l->reconnects++;reconnections++;return 1;
}
int __wrap_input_live_drain(InputLive *l,InputFrameSink sink,void *context){
    unsigned i=delivered_index;InputFrame f={.sequence=i+1};int x=100,y=200,down=(i%2)==0;
    if(i==2||i==3||i==12||i==13){x=640;y=(int)viewport_height-112;}
    if(i==4||i==5||i==14||i==15){x=640;y=364;}
    if(i==6||i==7||i==16||i==17){x=500;y=(int)viewport_height-91;}
    if(i==8){f.syn_dropped=1;f.suppressed=1;l->syn_dropped++;down=0;}
    if(down){f.contact_count=1;f.contacts[0]=(InputContact){0,x,y};}
    delivered_index++;l->frames++;
    return sink(context,&f,virtual_ns)?1:-1;
}
static int startup_policy_tests(void) {
    InputLive input={0};
    InputLiveConfig config={.expected_name="ilitek_ts",.width=1024,.height=600,
        .expected_raw_min=0,.expected_raw_max=16384,.expected_slots=10};
    unsigned attempts=0;
    startup_failures=2;startup_error="INPUT_NAME_MISMATCH";discovery_calls=0;
    if(!discover_input(&input,"/dev/input",&config,3000,&attempts)||attempts!=3)return 0;
    __wrap_input_live_close(&input);
    startup_failures=100;startup_error="INPUT_DEVICE_NOT_FOUND";startup_errno=ENOENT;discovery_calls=0;
    uint64_t before=virtual_ns;
    if(discover_input(&input,"/dev/input",&config,1000,&attempts)||attempts!=3||
       virtual_ns-before>1010000000ULL)return 0;
    startup_error="INPUT_DEVICE_PERMISSION";startup_errno=EACCES;discovery_calls=0;
    if(discover_input(&input,"/dev/input",&config,3000,&attempts)||attempts!=1||input.system_errno!=EACCES)return 0;
    startup_error="INPUT_PROFILE_MISMATCH";startup_errno=0;discovery_calls=0;
    if(discover_input(&input,"/dev/input",&config,3000,&attempts)||attempts!=1)return 0;
    startup_error="INPUT_DEVICE_NOT_FOUND";startup_errno=ENOENT;discovery_calls=0;
    if(discover_input(&input,"/dev/input",&config,0,&attempts)||attempts!=1)return 0;
    startup_failures=0;startup_error=NULL;startup_errno=0;discovery_calls=0;
    virtual_ns=1000000000ULL;
    puts("FRAMEWORK_STARTUP_POLICY_OK delayed-device bounded-timeout terminal-permission terminal-profile zero-wait");
    return 1;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    viewport_height=(unsigned)atoi(argv[3]);
    if(!startup_policy_tests())return 1;
    const char *profile=viewport_height==600?"imx6ul-1024x600":"imx6ul-1024x800";
    char *args[]={"ui-framework","--profile",(char *)profile,"--asset-root",argv[1],"--output",argv[2],"--seconds","8",
      "--allow-write","I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER","--touch-name","ilitek_ts","--raw-min","0","--raw-max","16384",
      "--slots","10","--swap-xy","0","--invert-x","0","--invert-y","0"};
    int rc=framework_program_main((int)(sizeof(args)/sizeof(args[0])),args);
    if(rc||unblanks!=1||invalid_frame||flushes<4||reconnections!=1||delivered_index!=18||host_alloc_stats().live_bytes){
        fprintf(stderr,"FRAMEWORK_LOOP_FAIL rc=%d unblank=%u flushes=%u reconnect=%u input=%u\n",rc,unblanks,flushes,reconnections,delivered_index);return 1;
    }
    puts("FRAMEWORK_LOOP_OK synthetic-io real-production-main real-renderer");return 0;
}
