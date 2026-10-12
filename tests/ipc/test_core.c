#define _GNU_SOURCE
#include "hosts/linux/ipc/broker.h"
#include "hosts/linux/ipc/client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"IPC_CORE_FAIL line=%d %s\n",__LINE__,#x);return 0;} } while(0)
typedef struct {
    PocketIpcBroker broker;PocketIpcClient a,b;PocketIpcChannel provider;
    char directory[80],path[108];uint64_t now,broker_epoch,provider_epoch,service_epoch;
} Fixture;

static int codec(void) {
    PocketIpcMessage source={0},out;source.kind=POCKET_IPC_COMMAND;source.flags=0;
    source.broker_epoch=UINT64_C(0x0102030405060708);source.connection_epoch=7;source.request_id=9;
    source.timeout_ms=1200;strcpy(source.service,"machine.v1");strcpy(source.member,"start");strcpy(source.idempotency_key,"stable-operation-1");
    const char payload[]="\001\t\n\"\\\xe4\xbd\xa0\xe5\xa5\xbd";
    memcpy(source.payload,payload,sizeof(payload));source.payload_length=sizeof(payload)-1u;
    unsigned char bytes[POCKET_IPC_FRAME_MAX];size_t n=0;
    CHECK(pocket_ipc_encode(&source,bytes,sizeof(bytes),&n)==POCKET_IPC_OK);
    CHECK(bytes[4]=='P'&&bytes[5]=='I'&&bytes[6]=='P'&&bytes[7]=='C');
    CHECK(bytes[8]==0&&bytes[9]==1&&bytes[10]==0&&bytes[11]==POCKET_IPC_COMMAND);
    CHECK(bytes[20]==1&&bytes[27]==8); /* fixed big-endian broker epoch */
    CHECK(pocket_ipc_decode(bytes,n,&out)==POCKET_IPC_OK);
    CHECK(out.broker_epoch==source.broker_epoch&&out.request_id==9&&out.payload_length==source.payload_length);
    CHECK(!memcmp(out.payload,payload,sizeof(payload))&&!strcmp(out.idempotency_key,"stable-operation-1"));
    CHECK(pocket_ipc_decode(bytes,n-1u,&out)==POCKET_IPC_PROTOCOL&&out.kind==0);
    bytes[9]=2;CHECK(pocket_ipc_decode(bytes,n,&out)==POCKET_IPC_PROTOCOL);bytes[9]=1;
    CHECK(!pocket_ipc_utf8_valid("\xc0\xaf",2)&&!pocket_ipc_utf8_valid("\xed\xa0\x80",3));
    CHECK(!pocket_ipc_utf8_valid("\xf4\x90\x80\x80",4)&&!pocket_ipc_utf8_valid("a\0b",3));
    CHECK(pocket_ipc_utf8_valid("\xf0\x9f\x98\x80",4));
    memset(source.payload,'x',POCKET_IPC_PAYLOAD_MAX);source.payload_length=POCKET_IPC_PAYLOAD_MAX;
    CHECK(pocket_ipc_encode(&source,bytes,sizeof(bytes),&n)==POCKET_IPC_OK);
    source.payload_length++;CHECK(pocket_ipc_encode(&source,bytes,sizeof(bytes),&n)==POCKET_IPC_INVALID);
    PocketIpcFilter all={0},wanted={"zone","north"},good={"zone","north"},bad={"zone","south"};
    CHECK(pocket_ipc_filter_matches(&all,&bad)&&pocket_ipc_filter_matches(&wanted,&good)&&!pocket_ipc_filter_matches(&wanted,&bad));
    memset(bad.key,'x',sizeof(bad.key));CHECK(!pocket_ipc_filter_valid(&bad));
    return 1;
}
static int fragmented_transport(void) {
    int fd[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));
    PocketIpcChannel receiver={0};CHECK(pocket_ipc_channel_adopt(&receiver,fd[1])==POCKET_IPC_OK);
    PocketIpcMessage m={0},out;m.kind=POCKET_IPC_REPLY;m.request_id=42;strcpy(m.service,"demo.v1");strcpy(m.member,"read");
    strcpy(m.payload,"reply");m.payload_length=5;
    unsigned char bytes[POCKET_IPC_FRAME_MAX];size_t n=0;CHECK(pocket_ipc_encode(&m,bytes,sizeof(bytes),&n)==POCKET_IPC_OK);
    for(size_t i=0;i<n;i++){
        CHECK(write(fd[0],bytes+i,1)==1);
        CHECK(pocket_ipc_channel_next(&receiver,&out)==(i+1u==n?POCKET_IPC_OK:POCKET_IPC_AGAIN));
    }
    CHECK(out.request_id==42&&!strcmp(out.payload,"reply"));
    close(fd[0]);CHECK(pocket_ipc_channel_next(&receiver,&out)==POCKET_IPC_DISCONNECTED);pocket_ipc_channel_close(&receiver);
    CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));
    CHECK(pocket_ipc_channel_adopt(&receiver,fd[1])==POCKET_IPC_OK);
    unsigned char oversized[]={0xff,0xff,0xff,0xff};CHECK(write(fd[0],oversized,4)==4);
    CHECK(pocket_ipc_channel_next(&receiver,&out)==POCKET_IPC_PROTOCOL);close(fd[0]);pocket_ipc_channel_close(&receiver);
    CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));
    CHECK(pocket_ipc_channel_adopt(&receiver,fd[1])==POCKET_IPC_OK);
    PocketIpcMessage batch[POCKET_IPC_TX_CAPACITY];for(unsigned i=0;i<POCKET_IPC_TX_CAPACITY;i++)batch[i]=m;
    CHECK(pocket_ipc_channel_send_batch(&receiver,batch,POCKET_IPC_TX_CAPACITY)==POCKET_IPC_OK);
    CHECK(!pocket_ipc_channel_available(&receiver)&&pocket_ipc_channel_send(&receiver,&m)==POCKET_IPC_BUSY);
    close(fd[0]);CHECK(pocket_ipc_channel_step(&receiver,1)==POCKET_IPC_DISCONNECTED);pocket_ipc_channel_close(&receiver);
    CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,fd));
    CHECK(pocket_ipc_channel_adopt(&receiver,fd[1])==POCKET_IPC_OK);
    batch[1].payload_length=POCKET_IPC_PAYLOAD_MAX+1u;
    CHECK(pocket_ipc_channel_send_batch(&receiver,batch,2)==POCKET_IPC_INVALID);
    CHECK(pocket_ipc_channel_available(&receiver)==POCKET_IPC_TX_CAPACITY);
    close(fd[0]);pocket_ipc_channel_close(&receiver);return 1;
}
static int pump(Fixture *f,unsigned turns) {
    for(unsigned i=0;i<turns;i++){
        CHECK(pocket_ipc_broker_step(&f->broker,f->now,64)==POCKET_IPC_OK);
        if(f->provider.impl){PocketIpcStatus s=pocket_ipc_channel_step(&f->provider,0);CHECK(s==POCKET_IPC_OK||s==POCKET_IPC_AGAIN);}
        if(f->a.impl)(void)pocket_ipc_client_step(&f->a,f->now,64);
        if(f->b.impl)(void)pocket_ipc_client_step(&f->b,f->now,64);
    }
    return 1;
}
static int provider_message(Fixture *f,PocketIpcMessage *m) {
    for(unsigned i=0;i<100;i++){
        PocketIpcStatus status=pocket_ipc_channel_next(&f->provider,m);
        if(status==POCKET_IPC_OK)return 1;
        CHECK(status==POCKET_IPC_AGAIN);CHECK(pump(f,1));
    }
    CHECK(0);return 0;
}
static int client_message(Fixture *f,PocketIpcClient *client,PocketIpcMessage *m) {
    for(unsigned i=0;i<100;i++){
        PocketIpcStatus status=pocket_ipc_client_next(client,m);
        if(status==POCKET_IPC_OK)return 1;
        CHECK(status==POCKET_IPC_AGAIN);CHECK(pump(f,1));
    }
    CHECK(0);return 0;
}
static int register_provider(Fixture *f) {
    CHECK(pocket_ipc_channel_open(&f->provider,f->path)==POCKET_IPC_OK);
    PocketIpcMessage welcome;CHECK(provider_message(f,&welcome));CHECK(welcome.kind==POCKET_IPC_WELCOME);
    f->broker_epoch=welcome.broker_epoch;f->provider_epoch=welcome.connection_epoch;
    const char *names[]={"read","act","changed","items"};const uint32_t flags[]={1,2,4,8};
    for(unsigned i=0;i<4;i++){
        PocketIpcMessage m={0};m.kind=POCKET_IPC_REGISTER;m.broker_epoch=f->broker_epoch;m.connection_epoch=f->provider_epoch;
        m.request_id=i+1u;m.flags=flags[i];strcpy(m.service,"probe.v1");strcpy(m.member,names[i]);
        CHECK(pocket_ipc_channel_send(&f->provider,&m)==POCKET_IPC_OK);
    }
    for(unsigned i=0;i<4;i++){
        PocketIpcMessage ack;CHECK(provider_message(f,&ack));CHECK(ack.kind==POCKET_IPC_REGISTERED&&ack.status==POCKET_IPC_OK&&ack.service_epoch);
        if(!i)f->service_epoch=ack.service_epoch;else CHECK(ack.service_epoch==f->service_epoch);
    }
    return 1;
}
static int fixture_open(Fixture *f) {
    memset(f,0,sizeof(*f));strcpy(f->directory,"/tmp/pocket-ipc-core-XXXXXX");CHECK(mkdtemp(f->directory));
    CHECK(snprintf(f->path,sizeof(f->path),"%s/socket",f->directory)<(int)sizeof(f->path));f->now=10000;
    PocketIpcBrokerPolicy policy={f->path,geteuid(),geteuid()};CHECK(pocket_ipc_broker_open(&f->broker,&policy)==POCKET_IPC_OK);
    PocketIpcBroker duplicate={0};CHECK(pocket_ipc_broker_open(&duplicate,&policy)==POCKET_IPC_BUSY&&!duplicate.impl);
    PocketIpcClientConfig config={f->path,"test-app",POCKET_IPC_ALLOW_ALL};
    CHECK(pocket_ipc_client_open(&f->a,&config)==POCKET_IPC_OK&&pocket_ipc_client_open(&f->b,&config)==POCKET_IPC_OK);
    CHECK(register_provider(f)&&pump(f,3));PocketIpcClientStats a,b;
    CHECK(pocket_ipc_client_stats(&f->a,&a)==POCKET_IPC_OK&&pocket_ipc_client_stats(&f->b,&b)==POCKET_IPC_OK);
    CHECK(a.ready&&b.ready&&a.connection_epoch!=b.connection_epoch);return 1;
}
static int fixture_close(Fixture *f) {
    pocket_ipc_client_close(&f->a);pocket_ipc_client_close(&f->b);pocket_ipc_channel_close(&f->provider);pocket_ipc_broker_close(&f->broker);
    CHECK(access(f->path,F_OK)!=0);CHECK(!rmdir(f->directory));return 1;
}
static int respond(Fixture *f,const PocketIpcMessage *request,const char *payload) {
    PocketIpcMessage response=*request;response.kind=POCKET_IPC_REPLY;response.status=POCKET_IPC_OK;
    response.payload_length=(uint32_t)strlen(payload);memcpy(response.payload,payload,response.payload_length+1u);
    CHECK(pocket_ipc_channel_send(&f->provider,&response)==POCKET_IPC_OK);return 1;
}
static int correlation_cancel_timeout(void) {
    Fixture f;CHECK(fixture_open(&f));uint64_t a_id,b_id;
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","A",1,1000,f.now,&a_id)==POCKET_IPC_OK);
    CHECK(pocket_ipc_client_request(&f.b,POCKET_IPC_QUERY,"probe.v1","read","B",1,1000,f.now,&b_id)==POCKET_IPC_OK);
    CHECK(a_id==b_id);PocketIpcMessage first,second,result;
    CHECK(provider_message(&f,&first)&&provider_message(&f,&second));
    CHECK(first.connection_epoch!=second.connection_epoch&&first.kind==POCKET_IPC_QUERY&&second.kind==POCKET_IPC_QUERY);
    CHECK(respond(&f,&second,second.payload)&&respond(&f,&first,first.payload));
    CHECK(client_message(&f,&f.a,&result)&&result.status==POCKET_IPC_OK&&!strcmp(result.payload,"A"));
    CHECK(client_message(&f,&f.b,&result)&&result.status==POCKET_IPC_OK&&!strcmp(result.payload,"B"));
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_COMMAND,"probe.v1","act","opaque",6,1000,f.now,&a_id)==POCKET_IPC_INVALID);
    CHECK(pocket_ipc_client_request_keyed(&f.a,POCKET_IPC_COMMAND,"probe.v1","act","same-operation","opaque",6,1000,f.now,&a_id)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&first)&&first.kind==POCKET_IPC_COMMAND&&!strcmp(first.idempotency_key,"same-operation")&&!strcmp(first.payload,"opaque"));
    CHECK(pocket_ipc_client_cancel(&f.a,a_id)==POCKET_IPC_OK);
    CHECK(pocket_ipc_client_cancel(&f.a,a_id)==POCKET_IPC_NOT_FOUND);
    CHECK(client_message(&f,&f.a,&result)&&result.status==POCKET_IPC_CANCELLED&&(result.flags&POCKET_IPC_OUTCOME_UNKNOWN));
    CHECK(provider_message(&f,&second)&&second.kind==POCKET_IPC_CANCEL);
    CHECK(respond(&f,&first,"late")&&pump(&f,5));CHECK(pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_AGAIN);
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","",0,10,f.now,&a_id)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&first));f.now+=10;
    CHECK(client_message(&f,&f.a,&result)&&result.status==POCKET_IPC_TIMEOUT&&result.request_id==a_id);
    CHECK(provider_message(&f,&second)&&second.kind==POCKET_IPC_CANCEL);
    CHECK(respond(&f,&first,"too-late")&&pump(&f,5));CHECK(pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_AGAIN);
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"absent.v1","read","",0,100,f.now,&a_id)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&result.status==POCKET_IPC_UNAVAILABLE);
    PocketIpcClientStats stats;CHECK(pocket_ipc_client_stats(&f.a,&stats)==POCKET_IPC_OK&&stats.ready&&!stats.pending);
    CHECK(fixture_close(&f));return 1;
}
static PocketIpcMessage published(const Fixture *f,PocketIpcKind kind,const char *member,const char *zone) {
    PocketIpcMessage m={0};m.kind=kind;m.broker_epoch=f->broker_epoch;m.connection_epoch=f->provider_epoch;m.service_epoch=f->service_epoch;
    strcpy(m.service,"probe.v1");strcpy(m.member,member);strcpy(m.filter.key,"zone");strcpy(m.filter.value,zone);return m;
}
static int filtering_state_and_close(void) {
    Fixture f;CHECK(fixture_open(&f));PocketIpcFilter north={"zone","north"};uint64_t subscription;
    CHECK(pocket_ipc_client_subscribe(&f.a,POCKET_IPC_SUBSCRIBE,"probe.v1","changed",&north,1000,f.now,&subscription)==POCKET_IPC_OK);
    PocketIpcMessage result;CHECK(client_message(&f,&f.a,&result)&&result.kind==POCKET_IPC_SUBSCRIBED);
    PocketIpcMessage e=published(&f,POCKET_IPC_EVENT,"changed","south");e.cursor=1;
    CHECK(pocket_ipc_channel_send(&f.provider,&e)==POCKET_IPC_OK&&pump(&f,5));CHECK(pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_AGAIN);
    e=published(&f,POCKET_IPC_EVENT,"changed","north");e.cursor=7;
    CHECK(pocket_ipc_channel_send(&f.provider,&e)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&result.kind==POCKET_IPC_EVENT&&result.cursor==7&&result.request_id==subscription);
    CHECK(pocket_ipc_channel_send(&f.provider,&e)==POCKET_IPC_OK&&pump(&f,5));
    CHECK(pocket_ipc_client_unsubscribe(&f.a,subscription)==POCKET_IPC_OK&&pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_AGAIN);
    CHECK(pump(&f,5));
    CHECK(pocket_ipc_client_subscribe(&f.a,POCKET_IPC_WATCH,"probe.v1","items",&north,1000,f.now,&subscription)==POCKET_IPC_OK);
    PocketIpcMessage watch;CHECK(provider_message(&f,&watch)&&watch.kind==POCKET_IPC_WATCH&&!strcmp(watch.filter.value,"north"));
    PocketIpcMessage snapshot[3]={watch,watch,watch};
    for(unsigned i=0;i<3;i++){snapshot[i].kind=POCKET_IPC_SNAPSHOT;snapshot[i].revision=4;snapshot[i].flags=0;}
    snapshot[0].flags=POCKET_IPC_SNAPSHOT_BEGIN;snapshot[1].cursor=22;snapshot[2].flags=POCKET_IPC_SNAPSHOT_END;
    CHECK(pocket_ipc_channel_send_batch(&f.provider,snapshot,3)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&(result.flags&POCKET_IPC_SNAPSHOT_BEGIN));
    CHECK(client_message(&f,&f.a,&result)&&result.cursor==22&&result.revision==4);
    CHECK(client_message(&f,&f.a,&result)&&(result.flags&POCKET_IPC_SNAPSHOT_END));
    PocketIpcMessage removal=watch;removal.kind=POCKET_IPC_STATE_REMOVE;removal.cursor=22;removal.revision=5;
    CHECK(pocket_ipc_channel_send(&f.provider,&removal)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&result.kind==POCKET_IPC_STATE_REMOVE&&result.cursor==22);
    pocket_ipc_client_close(&f.a);CHECK(provider_message(&f,&result)&&result.kind==POCKET_IPC_UNWATCH&&result.request_id==subscription);
    CHECK(fixture_close(&f));return 1;
}
static int reconnect_and_reservations(void) {
    Fixture f;CHECK(fixture_open(&f));uint64_t old_id,new_id;PocketIpcMessage old_request,new_request,result;
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","old",3,1000,f.now,&old_id)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&old_request));CHECK(pocket_ipc_client_reconnect(&f.a)==POCKET_IPC_OK&&pump(&f,5));
    CHECK(client_message(&f,&f.a,&result)&&result.request_id==old_id&&result.status==POCKET_IPC_DISCONNECTED&&
          result.broker_epoch==old_request.broker_epoch&&result.connection_epoch==old_request.connection_epoch);
    CHECK(provider_message(&f,&result)&&result.kind==POCKET_IPC_CANCEL);
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","new",3,1000,f.now,&new_id)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&new_request)&&new_request.connection_epoch!=old_request.connection_epoch&&new_id!=old_id);
    CHECK(respond(&f,&old_request,"stale")&&respond(&f,&new_request,"fresh"));
    CHECK(client_message(&f,&f.a,&result)&&result.request_id==new_id&&!strcmp(result.payload,"fresh"));
    CHECK(pump(&f,3)&&pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_AGAIN);
    PocketIpcFilter all={0};uint64_t sub;
    CHECK(pocket_ipc_client_subscribe(&f.a,POCKET_IPC_SUBSCRIBE,"probe.v1","changed",&all,1000,f.now,&sub)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&result.kind==POCKET_IPC_SUBSCRIBED);
    for(unsigned i=0;i<POCKET_IPC_PENDING_CAPACITY;i++){
        CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","",0,60000,f.now,&new_id)==POCKET_IPC_OK);
        CHECK(provider_message(&f,&new_request)&&new_request.kind==POCKET_IPC_QUERY);
    }
    CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","",0,60000,f.now,&new_id)==POCKET_IPC_BUSY);
    for(unsigned i=0;i<32;i++){
        PocketIpcMessage event=published(&f,POCKET_IPC_EVENT,"changed","north");event.cursor=i+1u;
        CHECK(pocket_ipc_channel_send(&f.provider,&event)==POCKET_IPC_OK&&pump(&f,3));
    }
    PocketIpcClientStats stats;CHECK(pocket_ipc_client_stats(&f.a,&stats)==POCKET_IPC_OK);
    CHECK(!stats.ready&&!stats.pending&&!stats.subscriptions&&stats.queued==POCKET_IPC_INBOX_CAPACITY);
    unsigned errors=0,down=0,events=0;
    while(pocket_ipc_client_next(&f.a,&result)==POCKET_IPC_OK){if(result.kind==POCKET_IPC_ERROR)errors++;else if(result.kind==POCKET_IPC_SERVICE_DOWN)down++;else if(result.kind==POCKET_IPC_EVENT)events++;}
    CHECK(errors==32&&down==1&&events==31);CHECK(fixture_close(&f));return 1;
}
static int unsubscribe_under_pressure(void) {
    Fixture f;CHECK(fixture_open(&f));uint64_t subscription,id;PocketIpcFilter all={0};PocketIpcMessage watch,result;
    CHECK(pocket_ipc_client_subscribe(&f.a,POCKET_IPC_WATCH,"probe.v1","items",&all,1000,f.now,&subscription)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&watch)&&watch.kind==POCKET_IPC_WATCH);
    PocketIpcMessage snapshot[2]={watch,watch};
    snapshot[0].kind=snapshot[1].kind=POCKET_IPC_SNAPSHOT;
    snapshot[0].flags=POCKET_IPC_SNAPSHOT_BEGIN;snapshot[1].flags=POCKET_IPC_SNAPSHOT_END;
    CHECK(pocket_ipc_channel_send_batch(&f.provider,snapshot,2)==POCKET_IPC_OK);
    CHECK(client_message(&f,&f.a,&result)&&client_message(&f,&f.a,&result));
    for(unsigned i=0;i<POCKET_IPC_TX_CAPACITY;i++)
        CHECK(pocket_ipc_client_request(&f.a,POCKET_IPC_QUERY,"probe.v1","read","",0,1000,f.now,&id)==POCKET_IPC_OK);
    /* UNWATCH cannot fit. Closing the connection must still release the remote
     * watch and settle every other accepted request once. */
    CHECK(pocket_ipc_client_unsubscribe(&f.a,subscription)==POCKET_IPC_OK);
    PocketIpcClientStats stats;CHECK(pocket_ipc_client_stats(&f.a,&stats)==POCKET_IPC_OK);
    CHECK(!stats.ready&&!stats.pending&&!stats.subscriptions&&stats.queued==POCKET_IPC_TX_CAPACITY);
    CHECK(provider_message(&f,&result)&&result.kind==POCKET_IPC_UNWATCH&&result.request_id==subscription);
    CHECK(fixture_close(&f));return 1;
}
static int duplicate_command_is_uncertain(void) {
    Fixture f;CHECK(fixture_open(&f));PocketIpcChannel raw={0};PocketIpcMessage hello,request={0},forwarded,result;
    CHECK(pocket_ipc_channel_open(&raw,f.path)==POCKET_IPC_OK&&pump(&f,3));
    CHECK(pocket_ipc_channel_next(&raw,&hello)==POCKET_IPC_OK&&hello.kind==POCKET_IPC_WELCOME);
    request.kind=POCKET_IPC_COMMAND;request.broker_epoch=hello.broker_epoch;request.connection_epoch=hello.connection_epoch;
    request.request_id=1;request.timeout_ms=1000;strcpy(request.service,"probe.v1");strcpy(request.member,"act");strcpy(request.idempotency_key,"physical-operation");
    CHECK(pocket_ipc_channel_send(&raw,&request)==POCKET_IPC_OK&&pocket_ipc_channel_step(&raw,0)==POCKET_IPC_OK);
    CHECK(provider_message(&f,&forwarded)&&forwarded.kind==POCKET_IPC_COMMAND);
    CHECK(pocket_ipc_channel_send(&raw,&request)==POCKET_IPC_OK&&pocket_ipc_channel_step(&raw,0)==POCKET_IPC_OK&&pump(&f,3));
    /* A duplicated correlation must not produce a rejection that could falsely
     * claim the original, already forwarded physical command was unexecuted. */
    CHECK(pocket_ipc_channel_next(&raw,&result)==POCKET_IPC_DISCONNECTED);
    CHECK(provider_message(&f,&result)&&result.kind==POCKET_IPC_CANCEL&&result.request_id==forwarded.request_id);
    pocket_ipc_channel_close(&raw);CHECK(fixture_close(&f));return 1;
}
static int authorization_and_provider_loss(void) {
    Fixture f;CHECK(fixture_open(&f));PocketIpcClient limited={0},denied={0};uint64_t id;
    PocketIpcClientConfig config={f.path,"limited",0};CHECK(pocket_ipc_client_open(&denied,&config)==POCKET_IPC_UNAUTHORIZED&&!denied.impl);
    config.allow=POCKET_IPC_ALLOW_QUERY;CHECK(pocket_ipc_client_open(&limited,&config)==POCKET_IPC_OK);
    CHECK(pocket_ipc_client_request_keyed(&limited,POCKET_IPC_COMMAND,"probe.v1","act","key","",0,1000,f.now,&id)==POCKET_IPC_UNAUTHORIZED);pocket_ipc_client_close(&limited);
    CHECK(pocket_ipc_client_request_keyed(&f.a,POCKET_IPC_COMMAND,"probe.v1","act","durable-key","",0,1000,f.now,&id)==POCKET_IPC_OK);
    PocketIpcMessage request,result;CHECK(provider_message(&f,&request));pocket_ipc_channel_close(&f.provider);
    CHECK(client_message(&f,&f.a,&result)&&result.status==POCKET_IPC_UNAVAILABLE&&(result.flags&POCKET_IPC_OUTCOME_UNKNOWN));
    uint64_t previous_service=f.service_epoch;CHECK(register_provider(&f)&&f.service_epoch!=previous_service);
    CHECK(pump(&f,5)&&pocket_ipc_channel_next(&f.provider,&result)==POCKET_IPC_AGAIN); /* no implicit Command replay */
    CHECK(fixture_close(&f));
    memset(&f,0,sizeof(f));strcpy(f.directory,"/tmp/pocket-ipc-core-XXXXXX");CHECK(mkdtemp(f.directory));
    CHECK(snprintf(f.path,sizeof(f.path),"%s/socket",f.directory)<(int)sizeof(f.path));f.now=10000;
    PocketIpcBrokerPolicy policy={f.path,geteuid(),(uid_t)(geteuid()+1u)};CHECK(pocket_ipc_broker_open(&f.broker,&policy)==POCKET_IPC_OK);
    CHECK(pocket_ipc_channel_open(&f.provider,f.path)==POCKET_IPC_OK&&provider_message(&f,&result));
    PocketIpcMessage registration={0};registration.kind=POCKET_IPC_REGISTER;registration.flags=POCKET_IPC_ALLOW_QUERY;
    registration.broker_epoch=result.broker_epoch;registration.connection_epoch=result.connection_epoch;registration.request_id=1;
    strcpy(registration.service,"unauthorized.v1");strcpy(registration.member,"read");
    CHECK(pocket_ipc_channel_send(&f.provider,&registration)==POCKET_IPC_OK&&provider_message(&f,&result));
    CHECK(result.kind==POCKET_IPC_ERROR&&result.status==POCKET_IPC_UNAUTHORIZED);CHECK(fixture_close(&f));return 1;
}
int main(void) {
    if(!codec()||!fragmented_transport()||!correlation_cancel_timeout()||!filtering_state_and_close()||!reconnect_and_reservations()||
       !unsubscribe_under_pressure()||!duplicate_command_is_uncertain()||!authorization_and_provider_loss())return 1;
    puts("IPC_CORE_OK");return 0;
}
