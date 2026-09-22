#include "input/bridge.h"
#include <stdio.h>
#include <string.h>

static int failures, hit_calls;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); ++failures; return; } } while(0)
#define PASS(x) printf("PASS %s\n",x)

static int hit(void *context, float x, float y) {
    (void)context;
    ++hit_calls;
    return 1000 + (int)x + (int)y;
}
static InputFrame one(int id,int x,int y) {
    InputFrame f={0};
    f.contact_count=1;
    f.contacts[0]=(InputContact){.id=id,.x=x,.y=y};
    return f;
}
static InputFrame empty(void) { InputFrame f={0}; return f; }

static void move_coalesce(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns=0;
    hit_calls=0; CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame a=one(2,10,20), m=one(2,30,40);
    CHECK(input_bridge_ingest(&b,&a,100));
    CHECK(input_bridge_ingest(&b,&m,200));
    CHECK(hit_calls==1 && b.queue_count==2 && b.coalesced_frames==0);
    CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(ns==100 && out.contact_count==1 && out.contacts[0].id==2 &&
          out.contacts[0].x==10 && out.contacts[0].y==20 &&
          out.contacts[0].hit==1030);
    CHECK(input_bridge_next(&b,&out,&ns)&&ns==200&&out.contacts[0].x==30);
    m.contacts[0].x=40; CHECK(input_bridge_ingest(&b,&m,300));
    m.contacts[0].x=50; CHECK(input_bridge_ingest(&b,&m,400));
    CHECK(b.queue_count==1&&b.coalesced_frames==1&&hit_calls==1);
    PASS("bridge-down-move-coalesce-hit-once");
}
static void fast_tap(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    hit_calls=0; CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame d=one(0,5,6), u=empty();
    CHECK(input_bridge_ingest(&b,&d,10));
    CHECK(input_bridge_ingest(&b,&u,11));
    CHECK(b.queue_count==2);
    CHECK(input_bridge_next(&b,&out,&ns) && out.contact_count==1 && ns==10);
    CHECK(input_bridge_next(&b,&out,&ns) && out.contact_count==0 && ns==11);
    CHECK(hit_calls==1);
    PASS("bridge-fast-tap-preserves-down-up");
}
static void cancellation(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame d=one(4,10,10); CHECK(input_bridge_ingest(&b,&d,1));
    CHECK(input_bridge_next(&b,&out,&ns) && out.contact_count==1);
    InputFrame c={0}; c.cancelled_count=1; c.cancelled[0]=4; c.syn_dropped=1;
    CHECK(input_bridge_ingest(&b,&c,2));
    CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(out.contact_count==0 && out.cancelled_count==1 && out.cancelled[0]==4);
    CHECK(input_bridge_next(&b,&out,&ns) && out.cancelled_count==0);
    PASS("bridge-cancel-terminal-once");
}
static void normal_release_no_cancel(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame d=one(1,50,60); InputFrame u=empty();
    CHECK(input_bridge_ingest(&b,&d,1)); CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(input_bridge_ingest(&b,&u,2)); CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(out.contact_count==0 && out.cancelled_count==0);
    PASS("bridge-normal-release-is-absence");
}
static void multi_contact_hits(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    hit_calls=0; CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame f={0}; f.contact_count=2;
    f.contacts[0]=(InputContact){.id=3,.x=10,.y=20};
    f.contacts[1]=(InputContact){.id=7,.x=30,.y=40};
    CHECK(input_bridge_ingest(&b,&f,1));
    CHECK(input_bridge_next(&b,&out,&ns) && out.contact_count==2);
    CHECK(hit_calls==2);
    f.contacts[0].x=11; f.contacts[1].y=41;
    CHECK(input_bridge_ingest(&b,&f,2));
    CHECK(input_bridge_next(&b,&out,&ns) && hit_calls==2);
    PASS("bridge-multicontact-hit-fact-stable");
}
static void queue_overflow_fail_closed(void) {
    InputBridge b; CHECK(input_bridge_init(&b,hit,NULL));
    for (int i=0;i<INPUT_BRIDGE_QUEUE;i++) {
        InputFrame f = (i%2)==0 ? one(0,1,1) : empty();
        CHECK(input_bridge_ingest(&b,&f,(uint64_t)i));
    }
    InputFrame extra=one(0,2,2);
    CHECK(!input_bridge_ingest(&b,&extra,100));
    CHECK(b.error && !strcmp(b.error,"INPUT_BRIDGE_QUEUE_OVERFLOW"));
    PASS("bridge-edge-queue-overflow-fail-closed");
}

static void priority_cancel(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame down=one(0,10,10),up=empty(),other=one(3,20,20);
    CHECK(input_bridge_ingest(&b,&down,1)); CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(input_bridge_ingest(&b,&up,2)); CHECK(input_bridge_ingest(&b,&other,3));
    CHECK(input_bridge_cancel_all(&b,4));
    CHECK(b.queue_count==1); CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(ns==4&&out.contact_count==0&&out.cancelled_count==1&&out.cancelled[0]==0);
    CHECK(input_bridge_next(&b,&out,&ns)&&!out.contact_count&&!out.cancelled_count);
    PASS("bridge-emergency-cancel-discards-stale-clicks");
}
static void unsent_cancel(void) {
    InputBridge b; PocketRuntimeContactsInput out; uint64_t ns;
    CHECK(input_bridge_init(&b,hit,NULL)); InputFrame down=one(0,10,10);
    CHECK(input_bridge_ingest(&b,&down,1));
    CHECK(input_bridge_cancel_all(&b,2)); CHECK(input_bridge_next(&b,&out,&ns));
    CHECK(!out.contact_count&&!out.cancelled_count);
    PASS("bridge-unsent-down-never-becomes-click-after-cancel");
}
static void invalid_wire(void) {
    InputBridge b; CHECK(input_bridge_init(&b,hit,NULL));
    InputFrame f=one(0,1024,0); hit_calls=0;
    CHECK(!input_bridge_ingest(&b,&f,1));CHECK(hit_calls==0&&b.queue_count==0);
    PASS("bridge-coordinate-wire-range-fail-closed");
}
int main(void) {
    priority_cancel(); unsent_cancel(); invalid_wire(); move_coalesce(); fast_tap(); cancellation(); normal_release_no_cancel();
    multi_contact_hits(); queue_overflow_fail_closed();
    if(failures){fprintf(stderr,"BRIDGE_FAILED failures=%d\n",failures);return 1;}
    printf("INPUT_BRIDGE_OK cases=9\n"); return 0;
}
