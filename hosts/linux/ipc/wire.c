#define _GNU_SOURCE
#include "wire.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define WIRE_MAGIC UINT32_C(0x50495043)
#define WIRE_FLAGS (POCKET_IPC_ALLOW_ALL | POCKET_IPC_SNAPSHOT_BEGIN | POCKET_IPC_SNAPSHOT_END | POCKET_IPC_OUTCOME_UNKNOWN)

static uint16_t get16(const unsigned char *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t get64(const unsigned char *p) { return (uint64_t)get32(p)<<32 | get32(p+4); }
static void put16(unsigned char *p,uint16_t v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void put32(unsigned char *p,uint32_t v) { p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v; }
static void put64(unsigned char *p,uint64_t v) { put32(p,(uint32_t)(v>>32));put32(p+4,(uint32_t)v); }

int pocket_ipc_name_valid(const char *name,size_t maximum) {
    if(!name||!maximum)return 0;
    size_t n=strnlen(name,maximum+1u);if(!n||n>maximum)return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return 0;
    }
    return 1;
}
int pocket_ipc_utf8_valid(const char *text,size_t length) {
    if(!text)return length==0;
    for(size_t i=0;i<length;) {
        unsigned char c=(unsigned char)text[i++];
        if(!c)return 0;
        if(c<0x80)continue;
        unsigned need;uint32_t value,minimum;
        if(c>=0xc2&&c<=0xdf){need=1;value=c&31u;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){need=2;value=c&15u;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){need=3;value=c&7u;minimum=0x10000;}
        else return 0;
        if(length-i<need)return 0;
        for(unsigned j=0;j<need;j++){unsigned char d=(unsigned char)text[i++];if((d&0xc0)!=0x80)return 0;value=(value<<6)|(d&63u);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0;
    }
    return 1;
}
int pocket_ipc_filter_valid(const PocketIpcFilter *f) {
    if(!f)return 0;
    size_t k=strnlen(f->key,sizeof(f->key)),v=strnlen(f->value,sizeof(f->value));
    if(k>POCKET_IPC_FILTER_KEY_MAX||v>POCKET_IPC_FILTER_VALUE_MAX)return 0;
    if(!k)return v==0;
    return pocket_ipc_name_valid(f->key,POCKET_IPC_FILTER_KEY_MAX)&&pocket_ipc_utf8_valid(f->value,v);
}
int pocket_ipc_filter_matches(const PocketIpcFilter *selector,const PocketIpcFilter *metadata) {
    return pocket_ipc_filter_valid(selector)&&pocket_ipc_filter_valid(metadata)&&
        (!selector->key[0]||(!strcmp(selector->key,metadata->key)&&!strcmp(selector->value,metadata->value)));
}
static int message_valid(const PocketIpcMessage *m) {
    if(!m||m->kind<POCKET_IPC_WELCOME||m->kind>POCKET_IPC_SERVICE_DOWN||
       m->status<POCKET_IPC_OK||m->status>POCKET_IPC_REJECTED||(m->flags&~WIRE_FLAGS)||
       m->payload_length>POCKET_IPC_PAYLOAD_MAX||!pocket_ipc_filter_valid(&m->filter))return 0;
    if((m->service[0]&&!pocket_ipc_name_valid(m->service,POCKET_IPC_NAME_MAX))||
       (m->member[0]&&!pocket_ipc_name_valid(m->member,POCKET_IPC_NAME_MAX))||
       (m->idempotency_key[0]&&!pocket_ipc_name_valid(m->idempotency_key,POCKET_IPC_NAME_MAX)))return 0;
    return pocket_ipc_utf8_valid(m->payload,m->payload_length);
}
PocketIpcStatus pocket_ipc_encode(const PocketIpcMessage *m,void *bytes,size_t capacity,size_t *written) {
    if(written)*written=0;
    if(!bytes||!written||!message_valid(m))return POCKET_IPC_INVALID;
    size_t lengths[5]={strlen(m->service),strlen(m->member),strlen(m->filter.key),strlen(m->filter.value),strlen(m->idempotency_key)};
    size_t body=POCKET_IPC_HEADER_BYTES+m->payload_length;
    for(unsigned i=0;i<5;i++)body+=lengths[i];
    if(capacity<body+4u)return POCKET_IPC_TOO_LARGE;
    unsigned char *b=bytes;put32(b,(uint32_t)body);b+=4;
    put32(b,WIRE_MAGIC);put16(b+4,POCKET_IPC_VERSION);put16(b+6,(uint16_t)m->kind);
    put32(b+8,m->flags);put32(b+12,(uint32_t)m->status);
    put64(b+16,m->broker_epoch);put64(b+24,m->connection_epoch);put64(b+32,m->request_id);
    put64(b+40,m->service_epoch);put64(b+48,m->revision);put64(b+56,m->cursor);put32(b+64,m->timeout_ms);
    for(unsigned i=0;i<4;i++)put16(b+68+i*2u,(uint16_t)lengths[i]);
    put32(b+76,m->payload_length);put16(b+80,(uint16_t)lengths[4]);
    const char *strings[5]={m->service,m->member,m->filter.key,m->filter.value,m->idempotency_key};
    size_t offset=POCKET_IPC_HEADER_BYTES;
    for(unsigned i=0;i<5;i++){memcpy(b+offset,strings[i],lengths[i]);offset+=lengths[i];}
    memcpy(b+offset,m->payload,m->payload_length);*written=body+4u;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_decode(const void *bytes,size_t length,PocketIpcMessage *out) {
    if(out)memset(out,0,sizeof(*out));
    if(!bytes||!out)return POCKET_IPC_INVALID;
    if(length<4u+POCKET_IPC_HEADER_BYTES||length>POCKET_IPC_FRAME_MAX)return POCKET_IPC_PROTOCOL;
    const unsigned char *b=bytes;
    if(get32(b)!=length-4u)return POCKET_IPC_PROTOCOL;
    b+=4;
    if(get32(b)!=WIRE_MAGIC||get16(b+4)!=POCKET_IPC_VERSION)return POCKET_IPC_PROTOCOL;
    size_t lengths[5]={get16(b+68),get16(b+70),get16(b+72),get16(b+74),get16(b+80)};
    size_t payload=get32(b+76),total=POCKET_IPC_HEADER_BYTES+payload;
    const size_t maximums[5]={POCKET_IPC_NAME_MAX,POCKET_IPC_NAME_MAX,POCKET_IPC_FILTER_KEY_MAX,POCKET_IPC_FILTER_VALUE_MAX,POCKET_IPC_NAME_MAX};
    for(unsigned i=0;i<5;i++){if(lengths[i]>maximums[i])return POCKET_IPC_PROTOCOL;total+=lengths[i];}
    if(payload>POCKET_IPC_PAYLOAD_MAX||total!=length-4u)return POCKET_IPC_PROTOCOL;
    PocketIpcMessage m={0};m.kind=(PocketIpcKind)get16(b+6);m.flags=get32(b+8);m.status=(PocketIpcStatus)get32(b+12);
    m.broker_epoch=get64(b+16);m.connection_epoch=get64(b+24);m.request_id=get64(b+32);
    m.service_epoch=get64(b+40);m.revision=get64(b+48);m.cursor=get64(b+56);m.timeout_ms=get32(b+64);
    char *strings[5]={m.service,m.member,m.filter.key,m.filter.value,m.idempotency_key};size_t offset=POCKET_IPC_HEADER_BYTES;
    for(unsigned i=0;i<5;i++){
        if(memchr(b+offset,0,lengths[i]))return POCKET_IPC_PROTOCOL;
        memcpy(strings[i],b+offset,lengths[i]);offset+=lengths[i];
    }
    m.payload_length=(uint32_t)payload;memcpy(m.payload,b+offset,payload);
    if(!message_valid(&m))return POCKET_IPC_PROTOCOL;
    *out=m;return POCKET_IPC_OK;
}
const char *pocket_ipc_status_name(PocketIpcStatus status) {
    static const char *const names[]={"OK","AGAIN","INVALID","UNAVAILABLE","UNAUTHORIZED","TIMEOUT","CANCELLED","DISCONNECTED","BUSY","TOO_LARGE","PROTOCOL","STALE","NOT_FOUND","INTERNAL","REJECTED"};
    return (unsigned)status<sizeof(names)/sizeof(names[0])?names[status]:"UNKNOWN";
}
uint64_t pocket_ipc_monotonic_ms(void) {
    struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}

typedef struct { unsigned char bytes[POCKET_IPC_FRAME_MAX];size_t length,offset; } Packet;
typedef struct {
    int fd,connecting;
    PocketIpcStatus status;
    unsigned char rx[POCKET_IPC_FRAME_MAX];size_t rx_used,rx_need;
    Packet tx[POCKET_IPC_TX_CAPACITY];size_t tx_head,tx_count;
} Channel;
static PocketIpcStatus channel_fail(Channel *c,PocketIpcStatus status) {
    if(c->fd>=0)close(c->fd);
    c->fd=-1;c->connecting=0;c->status=status;c->tx_count=0;c->rx_used=0;return status;
}
static PocketIpcStatus adopt(PocketIpcChannel *out,int fd,int connecting) {
    if(!out||out->impl||fd<0){if(fd>=0)close(fd);return POCKET_IPC_INVALID;}
    int flags=fcntl(fd,F_GETFL),descriptor=fcntl(fd,F_GETFD);
    if(flags<0||descriptor<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,descriptor|FD_CLOEXEC)){close(fd);return POCKET_IPC_INTERNAL;}
    Channel *c=calloc(1,sizeof(*c));if(!c){close(fd);return POCKET_IPC_INTERNAL;}
    c->fd=fd;c->connecting=connecting;c->rx_need=4;c->status=POCKET_IPC_OK;out->impl=c;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_channel_adopt(PocketIpcChannel *out,int fd) { return adopt(out,fd,0); }
PocketIpcStatus pocket_ipc_channel_open(PocketIpcChannel *out,const char *path) {
    if(!out||out->impl||!path||path[0]!='/'||strnlen(path,POCKET_IPC_SOCKET_PATH_MAX+1u)>POCKET_IPC_SOCKET_PATH_MAX)return POCKET_IPC_INVALID;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return POCKET_IPC_INTERNAL;
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;memcpy(address.sun_path,path,strlen(path)+1u);
    int connected=connect(fd,(struct sockaddr *)&address,sizeof(address));
    if(connected&&errno!=EINPROGRESS){int e=errno;close(fd);return e==EACCES?POCKET_IPC_UNAUTHORIZED:POCKET_IPC_UNAVAILABLE;}
    return adopt(out,fd,connected!=0);
}
PocketIpcStatus pocket_ipc_channel_step(PocketIpcChannel *out,size_t budget) {
    Channel *c=out?out->impl:NULL;if(!c)return POCKET_IPC_INVALID;if(c->fd<0)return c->status;
    if(c->connecting){
        struct pollfd p={c->fd,POLLOUT,0};int ready=poll(&p,1,0);
        if(ready<0)return errno==EINTR?POCKET_IPC_AGAIN:channel_fail(c,POCKET_IPC_DISCONNECTED);
        if(!ready)return POCKET_IPC_AGAIN;
        int error=0;socklen_t n=sizeof(error);
        if(getsockopt(c->fd,SOL_SOCKET,SO_ERROR,&error,&n)||error)return channel_fail(c,POCKET_IPC_DISCONNECTED);
        c->connecting=0;
    }
    if(!budget)budget=POCKET_IPC_FRAME_MAX*4u;
    if(budget>POCKET_IPC_FRAME_MAX*POCKET_IPC_TX_CAPACITY)budget=POCKET_IPC_FRAME_MAX*POCKET_IPC_TX_CAPACITY;
    while(c->tx_count&&budget){
        Packet *p=&c->tx[c->tx_head];size_t amount=p->length-p->offset;if(amount>budget)amount=budget;
        ssize_t n=send(c->fd,p->bytes+p->offset,amount,MSG_NOSIGNAL);
        if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return POCKET_IPC_AGAIN;return channel_fail(c,POCKET_IPC_DISCONNECTED);}
        if(!n)return channel_fail(c,POCKET_IPC_DISCONNECTED);
        p->offset+=(size_t)n;budget-=(size_t)n;
        if(p->offset==p->length){c->tx_head=(c->tx_head+1u)%POCKET_IPC_TX_CAPACITY;c->tx_count--;}
    }
    return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_channel_send_batch(PocketIpcChannel *out,const PocketIpcMessage *messages,size_t count) {
    Channel *c=out?out->impl:NULL;if(!c||!messages||!count)return POCKET_IPC_INVALID;
    if(c->fd<0)return c->status;
    if(count>POCKET_IPC_TX_CAPACITY-c->tx_count)return POCKET_IPC_BUSY;
    for(size_t i=0;i<count;i++){
        Packet *p=&c->tx[(c->tx_head+c->tx_count+i)%POCKET_IPC_TX_CAPACITY];size_t n=0;
        PocketIpcStatus status=pocket_ipc_encode(&messages[i],p->bytes,sizeof(p->bytes),&n);
        if(status!=POCKET_IPC_OK)return status;
        p->length=n;p->offset=0;
    }
    c->tx_count+=count;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_channel_send(PocketIpcChannel *out,const PocketIpcMessage *message) { return pocket_ipc_channel_send_batch(out,message,1); }
PocketIpcStatus pocket_ipc_channel_next(PocketIpcChannel *out,PocketIpcMessage *message) {
    Channel *c=out?out->impl:NULL;if(!c||!message)return POCKET_IPC_INVALID;
    if(c->fd<0)return c->status;
    if(c->connecting)return POCKET_IPC_AGAIN;
    unsigned interruptions=0;
    for(;;){
        if(c->rx_used==c->rx_need){
            if(c->rx_need==4u){uint32_t body=get32(c->rx);if(body<POCKET_IPC_HEADER_BYTES||body>POCKET_IPC_FRAME_MAX-4u)return channel_fail(c,POCKET_IPC_PROTOCOL);c->rx_need=4u+body;}
            else {PocketIpcStatus status=pocket_ipc_decode(c->rx,c->rx_need,message);c->rx_used=0;c->rx_need=4;
                return status==POCKET_IPC_OK?status:channel_fail(c,status);}
        }
        ssize_t n=recv(c->fd,c->rx+c->rx_used,c->rx_need-c->rx_used,0);
        if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return POCKET_IPC_AGAIN;if(errno==EINTR&&++interruptions<4u)continue;return errno==EINTR?POCKET_IPC_AGAIN:channel_fail(c,POCKET_IPC_DISCONNECTED);}
        if(!n)return channel_fail(c,POCKET_IPC_DISCONNECTED);
        c->rx_used+=(size_t)n;
    }
}
size_t pocket_ipc_channel_available(const PocketIpcChannel *out) { const Channel *c=out?out->impl:NULL;return c&&c->fd>=0?POCKET_IPC_TX_CAPACITY-c->tx_count:0; }
int pocket_ipc_channel_fd(const PocketIpcChannel *out) { const Channel *c=out?out->impl:NULL;return c?c->fd:-1; }
int pocket_ipc_channel_connected(const PocketIpcChannel *out) { const Channel *c=out?out->impl:NULL;return c&&c->fd>=0&&!c->connecting; }
void pocket_ipc_channel_close(PocketIpcChannel *out) { Channel *c=out?out->impl:NULL;if(!c)return;if(c->fd>=0)close(c->fd);memset(c,0,sizeof(*c));free(c);out->impl=NULL; }
