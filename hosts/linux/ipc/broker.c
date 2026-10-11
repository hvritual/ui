#define _GNU_SOURCE
#include "broker.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct { int live,doomed;uid_t uid;uint64_t epoch,last_id;PocketIpcChannel channel; } Peer;
typedef struct { int live;unsigned owner;uint64_t epoch;uint32_t allow;char service[64],member[64]; } Member;
typedef struct { int live;unsigned client,provider;uint64_t deadline;PocketIpcMessage message; } Route;
typedef struct { int live;unsigned client,provider;PocketIpcMessage message; } Watch;
typedef struct {
    int fd,bound,stepping;char path[POCKET_IPC_SOCKET_PATH_MAX+1u];dev_t device;ino_t inode;
    uid_t client_uid,service_uid;uint64_t epoch,next_peer,next_service,last_ms;unsigned rotation;
    Peer peers[POCKET_IPC_PEER_CAPACITY];Member members[POCKET_IPC_MEMBER_CAPACITY];
    Route routes[POCKET_IPC_ROUTE_CAPACITY];Watch watches[POCKET_IPC_BROKER_SUB_CAPACITY];
} Broker;

static int alive(const Broker *b,unsigned p) { return p<POCKET_IPC_PEER_CAPACITY&&b->peers[p].live&&!b->peers[p].doomed; }
static int send_required(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(!alive(b,peer))return 0;
    if(pocket_ipc_channel_send(&b->peers[peer].channel,m)!=POCKET_IPC_OK){b->peers[peer].doomed=1;return 0;}
    return 1;
}
static PocketIpcMessage result_for(const PocketIpcMessage *m,PocketIpcKind kind,PocketIpcStatus status) {
    PocketIpcMessage out=*m;out.kind=kind;out.status=status;out.payload_length=0;out.payload[0]=0;
    out.flags=m->kind==POCKET_IPC_COMMAND?POCKET_IPC_OUTCOME_UNKNOWN:0;return out;
}
static void reject(Broker *b,unsigned peer,const PocketIpcMessage *m,PocketIpcStatus status) {
    PocketIpcMessage result=result_for(m,POCKET_IPC_ERROR,status);
    /* A request rejected before forwarding has no uncertain physical outcome. */
    result.flags=0;result.broker_epoch=b->epoch;result.connection_epoch=b->peers[peer].epoch;
    (void)send_required(b,peer,&result);
}
static Member *find_member(Broker *b,const char *service,const char *member) {
    for(unsigned i=0;i<POCKET_IPC_MEMBER_CAPACITY;i++)if(b->members[i].live&&
        !strcmp(b->members[i].service,service)&&!strcmp(b->members[i].member,member))return &b->members[i];
    return NULL;
}
static void control(Broker *b,unsigned provider,const PocketIpcMessage *source,PocketIpcKind kind,int required) {
    if(!alive(b,provider))return;
    PocketIpcMessage m=*source;m.kind=kind;m.flags=0;m.payload_length=0;m.payload[0]=0;m.idempotency_key[0]=0;
    if(required)(void)send_required(b,provider,&m);
    else (void)pocket_ipc_channel_send(&b->peers[provider].channel,&m);
}
static void drop_peer(Broker *b,unsigned peer) {
    Peer *p=&b->peers[peer];if(!p->live)return;
    p->live=0;pocket_ipc_channel_close(&p->channel);
    for(unsigned i=0;i<POCKET_IPC_MEMBER_CAPACITY;i++)if(b->members[i].live&&b->members[i].owner==peer)b->members[i].live=0;
    for(unsigned i=0;i<POCKET_IPC_ROUTE_CAPACITY;i++){
        Route *r=&b->routes[i];if(!r->live)continue;
        if(r->client==peer){r->live=0;control(b,r->provider,&r->message,POCKET_IPC_CANCEL,0);}
        else if(r->provider==peer){r->live=0;PocketIpcMessage out=result_for(&r->message,POCKET_IPC_ERROR,POCKET_IPC_UNAVAILABLE);(void)send_required(b,r->client,&out);}
    }
    for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++){
        Watch *w=&b->watches[i];if(!w->live)continue;
        if(w->client==peer){w->live=0;if(w->message.kind==POCKET_IPC_WATCH)control(b,w->provider,&w->message,POCKET_IPC_UNWATCH,1);}
        else if(w->provider==peer){
            w->live=0;PocketIpcMessage out=result_for(&w->message,w->message.kind==POCKET_IPC_WATCH?POCKET_IPC_STATE_RESET:POCKET_IPC_SERVICE_DOWN,POCKET_IPC_UNAVAILABLE);
            (void)send_required(b,w->client,&out);
        }
    }
    memset(p,0,sizeof(*p));
}
static void sweep(Broker *b) {
    for(unsigned pass=0;pass<POCKET_IPC_PEER_CAPACITY;pass++){
        int changed=0;for(unsigned i=0;i<POCKET_IPC_PEER_CAPACITY;i++)if(b->peers[i].live&&b->peers[i].doomed){drop_peer(b,i);changed=1;}
        if(!changed)break;
    }
}
static int origin(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    return m->broker_epoch==b->epoch&&m->connection_epoch==b->peers[peer].epoch;
}
static int new_id(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    /* Reusing a correlation id is a connection protocol error. A rejection
     * using that id could falsely settle the already forwarded Command as
     * unexecuted, so close and leave its outcome explicitly uncertain. */
    if(!m->request_id||m->request_id<=b->peers[peer].last_id){b->peers[peer].doomed=1;return 0;}
    b->peers[peer].last_id=m->request_id;return 1;
}
static void register_member(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(!origin(b,peer,m)){b->peers[peer].doomed=1;return;}
    if(b->peers[peer].uid!=b->service_uid){reject(b,peer,m,POCKET_IPC_UNAUTHORIZED);return;}
    if(!new_id(b,peer,m))return;
    if(!pocket_ipc_name_valid(m->service,POCKET_IPC_NAME_MAX)||!pocket_ipc_name_valid(m->member,POCKET_IPC_NAME_MAX)||
       !m->flags||(m->flags&~POCKET_IPC_ALLOW_ALL)||m->status!=POCKET_IPC_OK||m->payload_length||m->filter.key[0]||m->idempotency_key[0]){reject(b,peer,m,POCKET_IPC_INVALID);return;}
    uint64_t epoch=0;Member *slot=NULL,*existing=find_member(b,m->service,m->member);
    for(unsigned i=0;i<POCKET_IPC_MEMBER_CAPACITY;i++){
        Member *entry=&b->members[i];if(!entry->live){if(!slot)slot=entry;continue;}
        if(!strcmp(entry->service,m->service)){
            if(entry->owner!=peer){reject(b,peer,m,POCKET_IPC_UNAUTHORIZED);return;}
            epoch=entry->epoch;
        }
    }
    if(existing){
        if(existing->allow!=m->flags){reject(b,peer,m,POCKET_IPC_INVALID);return;}
        slot=existing;
    }
    if(!slot){reject(b,peer,m,POCKET_IPC_BUSY);return;}
    if(m->service_epoch&&m->service_epoch!=epoch){reject(b,peer,m,POCKET_IPC_STALE);return;}
    if(!epoch){if(b->next_service==UINT64_MAX){reject(b,peer,m,POCKET_IPC_BUSY);return;}epoch=++b->next_service;}
    *slot=(Member){.live=1,.owner=peer,.epoch=epoch,.allow=m->flags};
    memcpy(slot->service,m->service,strlen(m->service)+1u);memcpy(slot->member,m->member,strlen(m->member)+1u);
    PocketIpcMessage ack=*m;ack.kind=POCKET_IPC_REGISTERED;ack.service_epoch=epoch;(void)send_required(b,peer,&ack);
}
static Member *admit(Broker *b,unsigned peer,const PocketIpcMessage *m,uint32_t permission) {
    if(!origin(b,peer,m)){b->peers[peer].doomed=1;return NULL;}
    if(b->peers[peer].uid!=b->client_uid){reject(b,peer,m,POCKET_IPC_UNAUTHORIZED);return NULL;}
    if(!new_id(b,peer,m))return NULL;
    if(!pocket_ipc_name_valid(m->service,POCKET_IPC_NAME_MAX)||!pocket_ipc_name_valid(m->member,POCKET_IPC_NAME_MAX)||
       m->status!=POCKET_IPC_OK||m->flags||!m->timeout_ms||m->timeout_ms>POCKET_IPC_TIMEOUT_MAX_MS){reject(b,peer,m,POCKET_IPC_INVALID);return NULL;}
    Member *entry=find_member(b,m->service,m->member);
    if(!entry||!alive(b,entry->owner)){reject(b,peer,m,POCKET_IPC_UNAVAILABLE);return NULL;}
    if(!(entry->allow&permission)){reject(b,peer,m,POCKET_IPC_UNAUTHORIZED);return NULL;}
    if(m->service_epoch&&m->service_epoch!=entry->epoch){reject(b,peer,m,POCKET_IPC_STALE);return NULL;}
    return entry;
}
static void request(Broker *b,unsigned peer,const PocketIpcMessage *m,uint64_t now) {
    Member *entry=admit(b,peer,m,m->kind==POCKET_IPC_QUERY?POCKET_IPC_ALLOW_QUERY:POCKET_IPC_ALLOW_COMMAND);if(!entry)return;
    if(m->filter.key[0]||(m->kind==POCKET_IPC_COMMAND?!m->idempotency_key[0]:m->idempotency_key[0]!=0)||now>UINT64_MAX-m->timeout_ms){reject(b,peer,m,POCKET_IPC_INVALID);return;}
    Route *route=NULL;for(unsigned i=0;i<POCKET_IPC_ROUTE_CAPACITY;i++)if(!b->routes[i].live){route=&b->routes[i];break;}
    if(!route){reject(b,peer,m,POCKET_IPC_BUSY);return;}
    *route=(Route){.live=1,.client=peer,.provider=entry->owner,.deadline=now+m->timeout_ms,.message=*m};route->message.service_epoch=entry->epoch;
    PocketIpcStatus status=pocket_ipc_channel_send(&b->peers[entry->owner].channel,&route->message);
    if(status!=POCKET_IPC_OK){route->live=0;reject(b,peer,m,status==POCKET_IPC_BUSY?status:POCKET_IPC_UNAVAILABLE);if(status!=POCKET_IPC_BUSY)b->peers[entry->owner].doomed=1;}
}
static void subscribe(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    Member *entry=admit(b,peer,m,m->kind==POCKET_IPC_SUBSCRIBE?POCKET_IPC_ALLOW_EVENT:POCKET_IPC_ALLOW_STATE);if(!entry)return;
    if(m->payload_length||m->idempotency_key[0]){reject(b,peer,m,POCKET_IPC_INVALID);return;}
    Watch *watch=NULL;for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++)if(!b->watches[i].live){watch=&b->watches[i];break;}
    if(!watch){reject(b,peer,m,POCKET_IPC_BUSY);return;}
    *watch=(Watch){.live=1,.client=peer,.provider=entry->owner,.message=*m};watch->message.service_epoch=entry->epoch;
    if(m->kind==POCKET_IPC_SUBSCRIBE){PocketIpcMessage ack=watch->message;ack.kind=POCKET_IPC_SUBSCRIBED;(void)send_required(b,peer,&ack);return;}
    PocketIpcStatus status=pocket_ipc_channel_send(&b->peers[entry->owner].channel,&watch->message);
    if(status!=POCKET_IPC_OK){watch->live=0;reject(b,peer,m,status==POCKET_IPC_BUSY?status:POCKET_IPC_UNAVAILABLE);if(status!=POCKET_IPC_BUSY)b->peers[entry->owner].doomed=1;}
}
static int same_route(const PocketIpcMessage *a,const PocketIpcMessage *m) {
    return a->broker_epoch==m->broker_epoch&&a->connection_epoch==m->connection_epoch&&a->request_id==m->request_id;
}
static int same_owner(const PocketIpcMessage *a,const PocketIpcMessage *m) {
    return a->service_epoch==m->service_epoch&&!strcmp(a->service,m->service)&&!strcmp(a->member,m->member);
}
static void cancel(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(!origin(b,peer,m)){b->peers[peer].doomed=1;return;}
    if(b->peers[peer].uid!=b->client_uid){reject(b,peer,m,POCKET_IPC_UNAUTHORIZED);return;}
    if(m->kind==POCKET_IPC_CANCEL){
        for(unsigned i=0;i<POCKET_IPC_ROUTE_CAPACITY;i++){
            Route *r=&b->routes[i];if(r->live&&r->client==peer&&same_route(&r->message,m)){
                r->live=0;control(b,r->provider,&r->message,POCKET_IPC_CANCEL,0);return;
            }
        }
    }else{
        for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++){
            Watch *w=&b->watches[i];if(w->live&&w->client==peer&&same_route(&w->message,m)){
                w->live=0;if(w->message.kind==POCKET_IPC_WATCH)control(b,w->provider,&w->message,POCKET_IPC_UNWATCH,1);return;
            }
        }
    }
}
static void reply(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(b->peers[peer].uid!=b->service_uid||m->broker_epoch!=b->epoch){b->peers[peer].doomed=1;return;}
    for(unsigned i=0;i<POCKET_IPC_ROUTE_CAPACITY;i++){
        Route *r=&b->routes[i];if(!r->live||!same_route(&r->message,m))continue;
        if(r->provider!=peer||!same_owner(&r->message,m)||m->status==POCKET_IPC_AGAIN){b->peers[peer].doomed=1;return;}
        r->live=0;(void)send_required(b,r->client,m);return;
    }
    if(m->kind==POCKET_IPC_ERROR){
        for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++){
            Watch *w=&b->watches[i];if(!w->live||!same_route(&w->message,m))continue;
            if(w->provider!=peer||!same_owner(&w->message,m)){b->peers[peer].doomed=1;return;}
            w->live=0;(void)send_required(b,w->client,m);return;
        }
    }
    /* Legitimate late responses after cancellation/timeout are discarded. */
}
static void event(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(b->peers[peer].uid!=b->service_uid||!origin(b,peer,m)){b->peers[peer].doomed=1;return;}
    Member *entry=find_member(b,m->service,m->member);
    if(!entry||entry->owner!=peer||entry->epoch!=m->service_epoch||!(entry->allow&POCKET_IPC_ALLOW_EVENT)||m->status!=POCKET_IPC_OK){b->peers[peer].doomed=1;return;}
    for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++){
        Watch *w=&b->watches[i];if(!w->live||w->provider!=peer||w->message.kind!=POCKET_IPC_SUBSCRIBE||!same_owner(&w->message,m)||
            !pocket_ipc_filter_matches(&w->message.filter,&m->filter))continue;
        PocketIpcMessage out=*m;out.connection_epoch=w->message.connection_epoch;out.request_id=w->message.request_id;(void)send_required(b,w->client,&out);
    }
}
static void state(Broker *b,unsigned peer,const PocketIpcMessage *m) {
    if(b->peers[peer].uid!=b->service_uid||m->broker_epoch!=b->epoch){b->peers[peer].doomed=1;return;}
    for(unsigned i=0;i<POCKET_IPC_BROKER_SUB_CAPACITY;i++){
        Watch *w=&b->watches[i];if(!w->live||!same_route(&w->message,m))continue;
        if(w->provider!=peer||w->message.kind!=POCKET_IPC_WATCH||!same_owner(&w->message,m)){b->peers[peer].doomed=1;return;}
        if(m->kind==POCKET_IPC_STATE_RESET)w->live=0;
        (void)send_required(b,w->client,m);return;
    }
}
static void dispatch(Broker *b,unsigned peer,const PocketIpcMessage *m,uint64_t now) {
    switch(m->kind){
        case POCKET_IPC_REGISTER:register_member(b,peer,m);break;
        case POCKET_IPC_QUERY:case POCKET_IPC_COMMAND:request(b,peer,m,now);break;
        case POCKET_IPC_SUBSCRIBE:case POCKET_IPC_WATCH:subscribe(b,peer,m);break;
        case POCKET_IPC_CANCEL:case POCKET_IPC_UNSUBSCRIBE:case POCKET_IPC_UNWATCH:cancel(b,peer,m);break;
        case POCKET_IPC_REPLY:case POCKET_IPC_ERROR:reply(b,peer,m);break;
        case POCKET_IPC_EVENT:event(b,peer,m);break;
        case POCKET_IPC_SNAPSHOT:case POCKET_IPC_STATE_DELTA:case POCKET_IPC_STATE_REMOVE:case POCKET_IPC_STATE_RESET:case POCKET_IPC_SUBSCRIBED:state(b,peer,m);break;
        default:b->peers[peer].doomed=1;break;
    }
}
static int private_parent(const char *path) {
    char parent[POCKET_IPC_SOCKET_PATH_MAX+1u];memcpy(parent,path,strlen(path)+1u);
    char *slash=strrchr(parent,'/');if(!slash)return 0;if(slash==parent)slash[1]=0;else *slash=0;
    struct stat st;return !lstat(parent,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0022);
}
PocketIpcStatus pocket_ipc_broker_open(PocketIpcBroker *out,const PocketIpcBrokerPolicy *policy) {
    if(!out||out->impl||!policy||!policy->socket_path||policy->socket_path[0]!='/'||
       strnlen(policy->socket_path,POCKET_IPC_SOCKET_PATH_MAX+1u)>POCKET_IPC_SOCKET_PATH_MAX||!private_parent(policy->socket_path))return POCKET_IPC_INVALID;
    struct stat st;if(!lstat(policy->socket_path,&st)||errno!=ENOENT)return POCKET_IPC_BUSY;
    Broker *b=calloc(1,sizeof(*b));if(!b)return POCKET_IPC_INTERNAL;b->fd=-1;
    b->client_uid=policy->client_uid;b->service_uid=policy->service_uid;memcpy(b->path,policy->socket_path,strlen(policy->socket_path)+1u);out->impl=b;
    int random=open("/dev/urandom",O_RDONLY|O_NONBLOCK|O_CLOEXEC);ssize_t n=-1;
    if(random>=0){n=read(random,&b->epoch,sizeof(b->epoch));close(random);}
    if(n!=(ssize_t)sizeof(b->epoch)||!b->epoch){pocket_ipc_broker_close(out);return POCKET_IPC_INTERNAL;}
    b->fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(b->fd<0){pocket_ipc_broker_close(out);return POCKET_IPC_INTERNAL;}
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;memcpy(address.sun_path,b->path,strlen(b->path)+1u);
    if(bind(b->fd,(struct sockaddr *)&address,sizeof(address))){PocketIpcStatus status=errno==EADDRINUSE?POCKET_IPC_BUSY:POCKET_IPC_INTERNAL;pocket_ipc_broker_close(out);return status;}
    if(lstat(b->path,&st)||!S_ISSOCK(st.st_mode)){pocket_ipc_broker_close(out);return POCKET_IPC_INTERNAL;}
    b->bound=1;b->device=st.st_dev;b->inode=st.st_ino;
    mode_t mode=policy->client_uid==geteuid()&&policy->service_uid==geteuid()?0600:0666;
    if(chmod(b->path,mode)||listen(b->fd,POCKET_IPC_PEER_CAPACITY)){pocket_ipc_broker_close(out);return POCKET_IPC_INTERNAL;}
    return POCKET_IPC_OK;
}
static void accept_peers(Broker *b) {
    for(unsigned attempt=0;attempt<POCKET_IPC_PEER_CAPACITY;attempt++){
        int fd=accept4(b->fd,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(fd<0){if(errno==EINTR)continue;break;}
        struct ucred credentials;socklen_t length=sizeof(credentials);
        if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&length)||length!=sizeof(credentials)||
           (credentials.uid!=b->client_uid&&credentials.uid!=b->service_uid)){close(fd);continue;}
        unsigned slot=POCKET_IPC_PEER_CAPACITY;
        for(unsigned i=0;i<POCKET_IPC_PEER_CAPACITY;i++)if(!b->peers[i].live){slot=i;break;}
        if(slot==POCKET_IPC_PEER_CAPACITY||b->next_peer==UINT64_MAX){close(fd);continue;}
        Peer *p=&b->peers[slot];memset(p,0,sizeof(*p));
        if(pocket_ipc_channel_adopt(&p->channel,fd)!=POCKET_IPC_OK)continue;
        p->live=1;p->uid=credentials.uid;p->epoch=++b->next_peer;
        PocketIpcMessage hello={0};hello.kind=POCKET_IPC_WELCOME;hello.broker_epoch=b->epoch;hello.connection_epoch=p->epoch;
        (void)send_required(b,slot,&hello);
    }
}
PocketIpcStatus pocket_ipc_broker_step(PocketIpcBroker *out,uint64_t now,uint32_t budget) {
    Broker *b=out?out->impl:NULL;if(!b||b->fd<0||b->stepping||now<b->last_ms)return POCKET_IPC_INVALID;
    b->stepping=1;b->last_ms=now;if(!budget)budget=64;if(budget>256)budget=256;
    for(unsigned i=0;i<POCKET_IPC_ROUTE_CAPACITY;i++){
        Route *r=&b->routes[i];if(!r->live||now<r->deadline)continue;
        r->live=0;control(b,r->provider,&r->message,POCKET_IPC_CANCEL,0);
        PocketIpcMessage result=result_for(&r->message,POCKET_IPC_ERROR,POCKET_IPC_TIMEOUT);(void)send_required(b,r->client,&result);
    }
    sweep(b);accept_peers(b);
    for(unsigned n=0;n<POCKET_IPC_PEER_CAPACITY;n++){
        unsigned i=(b->rotation+n)%POCKET_IPC_PEER_CAPACITY;if(!alive(b,i))continue;
        Peer *p=&b->peers[i];PocketIpcStatus status=pocket_ipc_channel_step(&p->channel,POCKET_IPC_FRAME_MAX*2u);
        if(status!=POCKET_IPC_OK&&status!=POCKET_IPC_AGAIN){p->doomed=1;continue;}
        for(unsigned j=0;j<4u&&budget&&alive(b,i);j++){
            PocketIpcMessage message;status=pocket_ipc_channel_next(&p->channel,&message);
            if(status==POCKET_IPC_AGAIN)break;
            if(status!=POCKET_IPC_OK){p->doomed=1;break;}
            budget--;dispatch(b,i,&message,now);
        }
    }
    b->rotation=(b->rotation+1u)%POCKET_IPC_PEER_CAPACITY;sweep(b);
    for(unsigned i=0;i<POCKET_IPC_PEER_CAPACITY;i++)if(alive(b,i)){
        PocketIpcStatus status=pocket_ipc_channel_step(&b->peers[i].channel,POCKET_IPC_FRAME_MAX*2u);
        if(status!=POCKET_IPC_OK&&status!=POCKET_IPC_AGAIN)b->peers[i].doomed=1;
    }
    sweep(b);b->stepping=0;return POCKET_IPC_OK;
}
int pocket_ipc_broker_fd(const PocketIpcBroker *out) { const Broker *b=out?out->impl:NULL;return b?b->fd:-1; }
void pocket_ipc_broker_close(PocketIpcBroker *out) {
    Broker *b=out?out->impl:NULL;if(!b)return;
    for(unsigned i=0;i<POCKET_IPC_PEER_CAPACITY;i++)pocket_ipc_channel_close(&b->peers[i].channel);
    if(b->fd>=0)close(b->fd);
    struct stat st;if(b->bound&&!lstat(b->path,&st)&&S_ISSOCK(st.st_mode)&&st.st_dev==b->device&&st.st_ino==b->inode)(void)unlink(b->path);
    memset(b,0,sizeof(*b));free(b);out->impl=NULL;
}
static volatile sig_atomic_t stopped;
static void stop_broker(int signal_number) { (void)signal_number;stopped=1; }
static int parse_uid(const char *text,uid_t *out) {
    if(!text||!text[0])return 0;
    for(const char *p=text;*p;p++)if(*p<'0'||*p>'9')return 0;
    errno=0;char *end=NULL;unsigned long long value=strtoull(text,&end,10);
    if(errno||!end||*end||value!=(unsigned long long)(uid_t)value)return 0;
    *out=(uid_t)value;return 1;
}
int pocket_ipc_broker_main(int argc,char **argv) {
    PocketIpcBrokerPolicy policy={0};int path=0,client=0,service=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")){puts("pocket-ipcd --socket ABSOLUTE_PATH --client-uid UID --service-uid UID");return 0;}
        if(i+1>=argc)return 2;
        if(!strcmp(argv[i],"--socket")&&!path){policy.socket_path=argv[++i];path=1;}
        else if(!strcmp(argv[i],"--client-uid")&&!client){if(!parse_uid(argv[++i],&policy.client_uid))return 2;client=1;}
        else if(!strcmp(argv[i],"--service-uid")&&!service){if(!parse_uid(argv[++i],&policy.service_uid))return 2;service=1;}
        else return 2;
    }
    if(!path||!client||!service)return 2;
    PocketIpcBroker broker={0};PocketIpcStatus status=pocket_ipc_broker_open(&broker,&policy);
    if(status!=POCKET_IPC_OK){fprintf(stderr,"IPC_BROKER_OPEN_%s\n",pocket_ipc_status_name(status));return 1;}
    struct sigaction action={0},old_int,old_term;action.sa_handler=stop_broker;sigemptyset(&action.sa_mask);stopped=0;
    if(sigaction(SIGINT,&action,&old_int)){pocket_ipc_broker_close(&broker);return 1;}
    if(sigaction(SIGTERM,&action,&old_term)){(void)sigaction(SIGINT,&old_int,NULL);pocket_ipc_broker_close(&broker);return 1;}
    int result=0;
    while(!stopped){
        if(pocket_ipc_broker_step(&broker,pocket_ipc_monotonic_ms(),64)!=POCKET_IPC_OK){result=1;break;}
        Broker *b=broker.impl;struct pollfd fds[POCKET_IPC_PEER_CAPACITY+1u];nfds_t count=1;
        fds[0]=(struct pollfd){b->fd,POLLIN,0};
        for(unsigned i=0;i<POCKET_IPC_PEER_CAPACITY;i++)if(alive(b,i)){
            short events=POLLIN;if(pocket_ipc_channel_available(&b->peers[i].channel)<POCKET_IPC_TX_CAPACITY)events|=POLLOUT;
            fds[count++]=(struct pollfd){pocket_ipc_channel_fd(&b->peers[i].channel),events,0};
        }
        if(poll(fds,count,10)<0&&errno!=EINTR){result=1;break;}
    }
    (void)sigaction(SIGINT,&old_int,NULL);(void)sigaction(SIGTERM,&old_term,NULL);pocket_ipc_broker_close(&broker);return result;
}
