#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include <stdlib.h>
#include <string.h>

#define SAFE_REQUEST_ID UINT64_C(9007199254740991)
#define HANDSHAKE_TIMEOUT_MS 5000u
typedef struct { int live;PocketIpcMessage message;uint64_t deadline; } Pending;
typedef struct { int live,acknowledged;PocketIpcMessage message;uint64_t deadline; } Subscription;
typedef struct {
    PocketIpcChannel channel;
    char path[POCKET_IPC_SOCKET_PATH_MAX+1u],application[POCKET_IPC_NAME_MAX+1u];
    uint32_t allow;
    uint64_t broker_epoch,connection_epoch,next_id,late,last_ms,handshake_deadline;
    int ready,stepping;
    PocketIpcStatus status;
    Pending pending[POCKET_IPC_PENDING_CAPACITY];
    Subscription subscriptions[POCKET_IPC_SUBSCRIPTION_CAPACITY];
    PocketIpcMessage inbox[POCKET_IPC_INBOX_CAPACITY];
    size_t inbox_head,inbox_count;
} Client;

static size_t reserved(const Client *c) {
    size_t count=0;
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(c->pending[i].live)count++;
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++)if(c->subscriptions[i].live)count++;
    return count;
}
static int enqueue(Client *c,const PocketIpcMessage *m,int ordinary) {
    /* Every accepted request/subscription reserves one terminal-result slot. */
    if(c->inbox_count>=POCKET_IPC_INBOX_CAPACITY||
       (ordinary&&c->inbox_count+reserved(c)>=POCKET_IPC_INBOX_CAPACITY))return 0;
    c->inbox[(c->inbox_head+c->inbox_count)%POCKET_IPC_INBOX_CAPACITY]=*m;c->inbox_count++;return 1;
}
static PocketIpcMessage terminal(const PocketIpcMessage *request,PocketIpcStatus status) {
    PocketIpcMessage result=*request;result.kind=POCKET_IPC_ERROR;result.status=status;
    result.payload_length=0;result.payload[0]=0;
    result.flags=request->kind==POCKET_IPC_COMMAND?POCKET_IPC_OUTCOME_UNKNOWN:0;
    return result;
}
static void finish_pending(Client *c,Pending *p,const PocketIpcMessage *result) {
    if(!p->live)return;
    p->live=0;(void)enqueue(c,result,0);
}
static void finish_subscription(Client *c,Subscription *s,const PocketIpcMessage *result) {
    if(!s->live)return;
    s->live=0;(void)enqueue(c,result,0);
}
static void disconnected(Client *c,PocketIpcStatus why) {
    c->ready=0;c->status=why;pocket_ipc_channel_close(&c->channel);
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(c->pending[i].live){PocketIpcMessage result=terminal(&c->pending[i].message,why);finish_pending(c,&c->pending[i],&result);}
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++)if(c->subscriptions[i].live){
        PocketIpcMessage result=terminal(&c->subscriptions[i].message,why);
        result.kind=c->subscriptions[i].message.kind==POCKET_IPC_WATCH?POCKET_IPC_STATE_RESET:POCKET_IPC_SERVICE_DOWN;
        finish_subscription(c,&c->subscriptions[i],&result);
    }
}
static int request_matches(const PocketIpcMessage *a,const PocketIpcMessage *b) {
    return a->request_id==b->request_id&&a->broker_epoch==b->broker_epoch&&
        a->connection_epoch==b->connection_epoch&&!strcmp(a->service,b->service)&&!strcmp(a->member,b->member)&&
        (!a->service_epoch||a->service_epoch==b->service_epoch);
}
static PocketIpcStatus send_control(Client *c,const PocketIpcMessage *original,PocketIpcKind kind) {
    PocketIpcMessage control=*original;control.kind=kind;control.payload_length=0;control.payload[0]=0;
    control.flags=0;control.idempotency_key[0]=0;return pocket_ipc_channel_send(&c->channel,&control);
}
static void expire(Client *c,uint64_t now) {
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++){
        Pending *p=&c->pending[i];if(!p->live||now<p->deadline)continue;
        (void)send_control(c,&p->message,POCKET_IPC_CANCEL);
        PocketIpcMessage result=terminal(&p->message,POCKET_IPC_TIMEOUT);finish_pending(c,p,&result);
    }
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++){
        Subscription *s=&c->subscriptions[i];if(!s->live||s->acknowledged||now<s->deadline)continue;
        PocketIpcStatus control_status=send_control(c,&s->message,s->message.kind==POCKET_IPC_WATCH?POCKET_IPC_UNWATCH:POCKET_IPC_UNSUBSCRIBE);
        PocketIpcMessage result=terminal(&s->message,POCKET_IPC_TIMEOUT);finish_subscription(c,s,&result);
        /* Unlike a finite request, a forgotten subscription can live forever.
         * If its control frame cannot be queued, peer teardown is the cleanup. */
        if(control_status!=POCKET_IPC_OK){disconnected(c,control_status);return;}
    }
}
static void incoming(Client *c,const PocketIpcMessage *m) {
    if(m->kind==POCKET_IPC_WELCOME){
        if(c->ready||!m->broker_epoch||!m->connection_epoch||m->request_id||m->status!=POCKET_IPC_OK){disconnected(c,POCKET_IPC_PROTOCOL);return;}
        c->broker_epoch=m->broker_epoch;c->connection_epoch=m->connection_epoch;c->ready=1;c->status=POCKET_IPC_OK;return;
    }
    if(!c->ready){disconnected(c,POCKET_IPC_PROTOCOL);return;}
    if(m->broker_epoch!=c->broker_epoch||m->connection_epoch!=c->connection_epoch){c->late++;return;}
    if(m->kind==POCKET_IPC_REPLY||m->kind==POCKET_IPC_ERROR){
        for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(c->pending[i].live&&request_matches(&c->pending[i].message,m)){
            finish_pending(c,&c->pending[i],m);return;
        }
    }
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++){
        Subscription *s=&c->subscriptions[i];if(!s->live||!request_matches(&s->message,m))continue;
        if(m->kind==POCKET_IPC_ERROR||m->kind==POCKET_IPC_SERVICE_DOWN||m->kind==POCKET_IPC_STATE_RESET){finish_subscription(c,s,m);return;}
        int valid=s->message.kind==POCKET_IPC_SUBSCRIBE?
            (m->kind==POCKET_IPC_SUBSCRIBED||m->kind==POCKET_IPC_EVENT):
            (m->kind==POCKET_IPC_SUBSCRIBED||m->kind==POCKET_IPC_SNAPSHOT||m->kind==POCKET_IPC_STATE_DELTA||m->kind==POCKET_IPC_STATE_REMOVE);
        if(!valid){disconnected(c,POCKET_IPC_PROTOCOL);return;}
        if(!m->service_epoch){disconnected(c,POCKET_IPC_PROTOCOL);return;}
        s->acknowledged=1;s->message.service_epoch=m->service_epoch;
        if(!enqueue(c,m,1))disconnected(c,POCKET_IPC_BUSY);
        return;
    }
    c->late++;
}
PocketIpcStatus pocket_ipc_client_open(PocketIpcClient *out,const PocketIpcClientConfig *config) {
    if(!out||out->impl||!config||!config->socket_path||config->socket_path[0]!='/'||
       strnlen(config->socket_path,POCKET_IPC_SOCKET_PATH_MAX+1u)>POCKET_IPC_SOCKET_PATH_MAX||
       (config->allow&~POCKET_IPC_ALLOW_ALL))return POCKET_IPC_INVALID;
    if(!config->allow)return POCKET_IPC_UNAUTHORIZED;
    if(config->application_id&&!pocket_ipc_name_valid(config->application_id,POCKET_IPC_NAME_MAX))return POCKET_IPC_INVALID;
    Client *c=calloc(1,sizeof(*c));if(!c)return POCKET_IPC_INTERNAL;
    memcpy(c->path,config->socket_path,strlen(config->socket_path)+1u);
    if(config->application_id)memcpy(c->application,config->application_id,strlen(config->application_id)+1u);
    c->allow=config->allow;out->impl=c;
    c->status=pocket_ipc_channel_open(&c->channel,c->path);
    return c->status;
}
PocketIpcStatus pocket_ipc_client_step(PocketIpcClient *out,uint64_t now,uint32_t budget) {
    Client *c=out?out->impl:NULL;if(!c||c->stepping||now<c->last_ms)return POCKET_IPC_INVALID;
    c->last_ms=now;c->stepping=1;
    if(!c->channel.impl){c->stepping=0;return c->status;}
    if(!budget)budget=16;
    if(budget>64)budget=64;
    if(!c->handshake_deadline)c->handshake_deadline=now>UINT64_MAX-HANDSHAKE_TIMEOUT_MS?UINT64_MAX:now+HANDSHAKE_TIMEOUT_MS;
    expire(c,now);
    if(!c->channel.impl){c->stepping=0;return c->status;}
    PocketIpcStatus status=pocket_ipc_channel_step(&c->channel,POCKET_IPC_FRAME_MAX*4u);
    if(status!=POCKET_IPC_OK&&status!=POCKET_IPC_AGAIN)disconnected(c,status);
    for(uint32_t i=0;c->channel.impl&&i<budget;i++){
        PocketIpcMessage message;status=pocket_ipc_channel_next(&c->channel,&message);
        if(status==POCKET_IPC_AGAIN)break;
        if(status!=POCKET_IPC_OK){disconnected(c,status);break;}
        incoming(c,&message);
    }
    if(c->channel.impl&&!c->ready&&now>=c->handshake_deadline)disconnected(c,POCKET_IPC_TIMEOUT);
    c->stepping=0;return c->ready?POCKET_IPC_OK:c->channel.impl?POCKET_IPC_AGAIN:c->status;
}
static PocketIpcStatus prepare(Client *c,PocketIpcKind kind,const char *service,const char *member,
    uint32_t permission,uint32_t timeout,uint64_t now,PocketIpcMessage *m) {
    if(!c||!m||!pocket_ipc_name_valid(service,POCKET_IPC_NAME_MAX)||!pocket_ipc_name_valid(member,POCKET_IPC_NAME_MAX)||
       !timeout||timeout>POCKET_IPC_TIMEOUT_MAX_MS||now>UINT64_MAX-timeout)return POCKET_IPC_INVALID;
    if(!(c->allow&permission))return POCKET_IPC_UNAUTHORIZED;
    if(!c->ready)return c->channel.impl?POCKET_IPC_AGAIN:POCKET_IPC_UNAVAILABLE;
    if(c->next_id>=SAFE_REQUEST_ID||c->inbox_count+reserved(c)>=POCKET_IPC_INBOX_CAPACITY)return POCKET_IPC_BUSY;
    memset(m,0,sizeof(*m));m->kind=kind;m->broker_epoch=c->broker_epoch;m->connection_epoch=c->connection_epoch;
    m->request_id=c->next_id+1u;m->timeout_ms=timeout;
    memcpy(m->service,service,strlen(service)+1u);memcpy(m->member,member,strlen(member)+1u);return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_client_request_keyed(PocketIpcClient *out,PocketIpcKind kind,
    const char *service,const char *member,const char *key,const char *payload,size_t length,
    uint32_t timeout,uint64_t now,uint64_t *id) {
    Client *c=out?out->impl:NULL;if(id)*id=0;
    if(!id||(kind!=POCKET_IPC_QUERY&&kind!=POCKET_IPC_COMMAND)||length>POCKET_IPC_PAYLOAD_MAX||!pocket_ipc_utf8_valid(payload,length)||
       (kind==POCKET_IPC_COMMAND&&!pocket_ipc_name_valid(key,POCKET_IPC_NAME_MAX))||
       (kind==POCKET_IPC_QUERY&&key&&key[0]))return POCKET_IPC_INVALID;
    PocketIpcMessage m;PocketIpcStatus status=prepare(c,kind,service,member,kind==POCKET_IPC_QUERY?POCKET_IPC_ALLOW_QUERY:POCKET_IPC_ALLOW_COMMAND,timeout,now,&m);
    if(status!=POCKET_IPC_OK)return status;
    Pending *p=NULL;for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(!c->pending[i].live){p=&c->pending[i];break;}
    if(!p)return POCKET_IPC_BUSY;
    if(key)memcpy(m.idempotency_key,key,strlen(key)+1u);
    if(length)memcpy(m.payload,payload,length);
    m.payload_length=(uint32_t)length;
    /* Correlation exists before the frame can become visible to transport. */
    *p=(Pending){1,m,now+timeout};
    status=pocket_ipc_channel_send(&c->channel,&m);
    if(status!=POCKET_IPC_OK){p->live=0;return status;}
    c->next_id=m.request_id;*id=m.request_id;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_client_request(PocketIpcClient *out,PocketIpcKind kind,
    const char *service,const char *member,const char *payload,size_t length,uint32_t timeout,uint64_t now,uint64_t *id) {
    return pocket_ipc_client_request_keyed(out,kind,service,member,NULL,payload,length,timeout,now,id);
}
PocketIpcStatus pocket_ipc_client_subscribe(PocketIpcClient *out,PocketIpcKind kind,const char *service,
    const char *member,const PocketIpcFilter *filter,uint32_t timeout,uint64_t now,uint64_t *id) {
    Client *c=out?out->impl:NULL;if(id)*id=0;
    if(!id||(kind!=POCKET_IPC_SUBSCRIBE&&kind!=POCKET_IPC_WATCH)||(filter&&!pocket_ipc_filter_valid(filter)))return POCKET_IPC_INVALID;
    PocketIpcMessage m;PocketIpcStatus status=prepare(c,kind,service,member,kind==POCKET_IPC_SUBSCRIBE?POCKET_IPC_ALLOW_EVENT:POCKET_IPC_ALLOW_STATE,timeout,now,&m);
    if(status!=POCKET_IPC_OK)return status;
    Subscription *s=NULL;for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++)if(!c->subscriptions[i].live){s=&c->subscriptions[i];break;}
    if(!s)return POCKET_IPC_BUSY;
    if(filter)m.filter=*filter;
    *s=(Subscription){1,0,m,now+timeout};status=pocket_ipc_channel_send(&c->channel,&m);
    if(status!=POCKET_IPC_OK){s->live=0;return status;}
    c->next_id=m.request_id;*id=m.request_id;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_client_cancel(PocketIpcClient *out,uint64_t id) {
    Client *c=out?out->impl:NULL;if(!c||!id)return POCKET_IPC_INVALID;
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(c->pending[i].live&&c->pending[i].message.request_id==id){
        Pending *p=&c->pending[i];(void)send_control(c,&p->message,POCKET_IPC_CANCEL);
        PocketIpcMessage result=terminal(&p->message,POCKET_IPC_CANCELLED);finish_pending(c,p,&result);return POCKET_IPC_OK;
    }
    return POCKET_IPC_NOT_FOUND;
}
static void purge_subscription(Client *c,uint64_t epoch,uint64_t id) {
    size_t keep=0;
    for(size_t i=0;i<c->inbox_count;i++){
        PocketIpcMessage *m=&c->inbox[(c->inbox_head+i)%POCKET_IPC_INBOX_CAPACITY];
        if(m->connection_epoch==epoch&&m->request_id==id)continue;
        c->inbox[(c->inbox_head+keep)%POCKET_IPC_INBOX_CAPACITY]=*m;keep++;
    }
    c->inbox_count=keep;
}
PocketIpcStatus pocket_ipc_client_unsubscribe(PocketIpcClient *out,uint64_t id) {
    Client *c=out?out->impl:NULL;if(!c||!id)return POCKET_IPC_INVALID;
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++)if(c->subscriptions[i].live&&c->subscriptions[i].message.request_id==id){
        Subscription *s=&c->subscriptions[i];s->live=0;purge_subscription(c,s->message.connection_epoch,id);
        PocketIpcStatus status=send_control(c,&s->message,s->message.kind==POCKET_IPC_WATCH?POCKET_IPC_UNWATCH:POCKET_IPC_UNSUBSCRIBE);
        if(status!=POCKET_IPC_OK)disconnected(c,status);
        return POCKET_IPC_OK;
    }
    return POCKET_IPC_NOT_FOUND;
}
PocketIpcStatus pocket_ipc_client_next(PocketIpcClient *out,PocketIpcMessage *message) {
    Client *c=out?out->impl:NULL;if(!c||!message)return POCKET_IPC_INVALID;if(!c->inbox_count)return POCKET_IPC_AGAIN;
    *message=c->inbox[c->inbox_head];c->inbox_head=(c->inbox_head+1u)%POCKET_IPC_INBOX_CAPACITY;c->inbox_count--;return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_client_stats(const PocketIpcClient *out,PocketIpcClientStats *stats) {
    const Client *c=out?out->impl:NULL;if(!c||!stats)return POCKET_IPC_INVALID;memset(stats,0,sizeof(*stats));
    stats->broker_epoch=c->broker_epoch;stats->connection_epoch=c->connection_epoch;stats->late_messages=c->late;
    stats->queued=(uint32_t)c->inbox_count;stats->ready=c->ready;stats->status=c->status;
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++)if(c->pending[i].live)stats->pending++;
    for(unsigned i=0;i<POCKET_IPC_SUBSCRIPTION_CAPACITY;i++)if(c->subscriptions[i].live)stats->subscriptions++;
    return POCKET_IPC_OK;
}
PocketIpcStatus pocket_ipc_client_reconnect(PocketIpcClient *out) {
    Client *c=out?out->impl:NULL;if(!c||c->stepping)return POCKET_IPC_INVALID;
    disconnected(c,POCKET_IPC_DISCONNECTED);c->broker_epoch=0;c->connection_epoch=0;c->handshake_deadline=0;
    c->status=pocket_ipc_channel_open(&c->channel,c->path);return c->status;
}
void pocket_ipc_client_close(PocketIpcClient *out) {
    Client *c=out?out->impl:NULL;if(!c)return;c->ready=0;pocket_ipc_channel_close(&c->channel);
    memset(c,0,sizeof(*c));free(c);out->impl=NULL;
}
