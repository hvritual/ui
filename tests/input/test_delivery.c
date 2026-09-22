#define _POSIX_C_SOURCE 200809L
#define POCKET_RUNTIME_HARNESS
#include "host.h"
#include "input/bridge.h"

#include <stdio.h>
#include <string.h>

static int failures;
/* runtime-test.o deliberately retains POCKET_RUNTIME_STAGE_HOOKS.
   Supply the test-owned observer; never disable the hooks to hide link errors. */
static unsigned stage_calls;
void pocket_bench_stage(int stage) { (void)stage; ++stage_calls; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); ++failures; goto cleanup; } } while(0)
#define PASS(x) printf("PASS %s\n",x)

static int inspect(int op) {
    int32_t out=-999;
    if(!pocket_runtime_harness_call(op,0,&out)){++failures;return -999;}
    return out;
}
static int runtime_hit(void *ctx,float x,float y) {
    (void)ctx;return pocket_runtime_hit_test_bounds(x,y);
}
static InputFrame one(int id,int x,int y) {
    InputFrame f={0};f.contact_count=1;f.contacts[0]=(InputContact){id,x,y};return f;
}
int main(int argc,char **argv) {
    const char *root=argc>1?argv[1]:"tests/input";
    LinuxHost host={0};HostFrame pixels;InputBridge bridge;
    PocketRuntimeContactsInput guest;uint64_t ns=0;
    int box=0;
    const int deliberate=argc>3&&!strcmp(argv[3],"--intentional-failure");
    CHECK(host_open(&host,"imx6ul-1024x600",root,"delivery-scene.js",NULL,0));
    CHECK(pocket_runtime_harness_bind("inspect"));
    CHECK(host_render(&host,&pixels));
    box=inspect(3);CHECK(box>0);
    CHECK(input_bridge_init(&bridge,runtime_hit,NULL));

    {
        InputFrame down=one(0,10,10);
        CHECK(input_bridge_ingest(&bridge,&down,100));
        CHECK(input_bridge_next(&bridge,&guest,&ns)&&ns==100);
        CHECK(guest.contact_count==1&&guest.contacts[0].hit==box);
        uint32_t packed=pocket_runtime_pack_contact(&guest.contacts[0]);
        stage_calls=0;
        CHECK(host_turn_contacts(&host,&guest));
        CHECK(stage_calls>0 && !deliberate);
        CHECK((uint32_t)inspect(1)==packed&&inspect(2)==box);
        CHECK(bridge.hit_queries==1);
        PASS("delivery-down-hit-fact-real-core");
    }
    {
        InputFrame move=one(0,20,20);
        CHECK(input_bridge_ingest(&bridge,&move,200));
        CHECK(input_bridge_next(&bridge,&guest,&ns));
        CHECK(guest.contacts[0].hit==box&&bridge.hit_queries==1);
        CHECK(host_turn_contacts(&host,&guest));
        CHECK(inspect(2)==box);
        PASS("delivery-move-retains-down-hit");
    }
    {
        InputFrame up={0};
        CHECK(input_bridge_ingest(&bridge,&up,300));
        CHECK(input_bridge_next(&bridge,&guest,&ns)&&guest.contact_count==0&&guest.cancelled_count==0);
        CHECK(host_turn_contacts(&host,&guest));
        CHECK(inspect(1)==0&&inspect(2)==-1);
        PASS("delivery-normal-release-empty-snapshot");
    }
    {
        InputFrame down=one(3,10,10);
        CHECK(input_bridge_ingest(&bridge,&down,400));
        CHECK(input_bridge_next(&bridge,&guest,&ns));
        CHECK(host_turn_contacts(&host,&guest));
        InputFrame cancelled={0};cancelled.cancelled_count=1;cancelled.cancelled[0]=3;cancelled.syn_dropped=1;
        CHECK(input_bridge_ingest(&bridge,&cancelled,500));
        CHECK(input_bridge_next(&bridge,&guest,&ns)&&guest.contact_count==0&&guest.cancelled_count==1);
        CHECK(host_turn_contacts(&host,&guest));
        CHECK((uint32_t)inspect(1)==pocket_runtime_pack_cancel(3)&&inspect(2)==-1);
        PASS("delivery-terminal-cancel-wire");
    }
    {
        InputFrame down=one(1,30,30),up={0};
        int before=inspect(4);
        CHECK(input_bridge_ingest(&bridge,&down,600));
        CHECK(input_bridge_ingest(&bridge,&up,601));
        CHECK(input_bridge_next(&bridge,&guest,&ns)&&guest.contact_count==1);
        CHECK(host_turn_contacts(&host,&guest));
        CHECK(input_bridge_next(&bridge,&guest,&ns)&&guest.contact_count==0);
        CHECK(host_turn_contacts(&host,&guest));
        CHECK(inspect(4)==before+2);
        PASS("delivery-fast-tap-two-guest-turns");
    }

    {
        InputFrame down=one(2,10,10),up={0};
        CHECK(input_bridge_ingest(&bridge,&down,700));
        CHECK(input_bridge_next(&bridge,&guest,&ns));CHECK(host_turn_contacts(&host,&guest));
        CHECK(input_bridge_ingest(&bridge,&up,701));
        CHECK(input_bridge_cancel_all(&bridge,702));
        CHECK(input_bridge_next(&bridge,&guest,&ns));CHECK(host_turn_contacts(&host,&guest));
        CHECK(guest.cancelled_count==1&&(uint32_t)inspect(1)==pocket_runtime_pack_cancel(2));
        PASS("delivery-focus-loss-cancels-instead-of-release-click");
    }
    host_close(&host);
    CHECK(argc>2);
    for (unsigned height=600;height<=800;height+=200) {
        CHECK(host_open(&host,height==600?"imx6ul-1024x600":"imx6ul-1024x800",root,"delivery-scene.js",NULL,0));
        CHECK(pocket_runtime_harness_bind("inspect"));CHECK(host_render(&host,&pixels));
        CHECK(input_bridge_init(&bridge,runtime_hit,NULL));
        InputTransform transform={.x={0,16384},.y={0,16384},.width=1024,.height=height};
        InputState state;CHECK(input_state_init(&state,INPUT_PROTOCOL_MT_B,10,&transform));
        FILE *trace=fopen(argv[2],"r");CHECK(trace);
        unsigned type,code,frames=0,events=0,active=0;int value;int good=1;
        while(fscanf(trace,"%u %u %d",&type,&code,&value)==3) {
            events++;int result=input_state_feed(&state,(uint16_t)type,(uint16_t)code,value);
            if(result==0||result==3){good=0;break;}
            if(result==2){frames++;const InputFrame *f=input_state_frame(&state);active+=f->contact_count!=0;
                if(!input_bridge_ingest(&bridge,f,events)||!input_bridge_next(&bridge,&guest,&ns)||!host_turn_contacts(&host,&guest)){good=0;break;}
                uint32_t want=guest.contact_count?pocket_runtime_pack_contact(&guest.contacts[0]):0;
                if((uint32_t)inspect(1)!=want){good=0;break;}
            }
        }
        int end=feof(trace);int closed=fclose(trace);CHECK(good&&end&&!closed);
        CHECK(events==650&&frames==249&&active==245&&bridge.hit_queries==4&&inspect(1)==0);
        printf("PASS delivery-real-trace-650-events-%u\n",height);
        host_close(&host);
    }
cleanup:
    host_close(&host);
    if(host_alloc_stats().live_bytes!=0){fprintf(stderr,"ALLOC_LEAK\n");failures++;}
    if(failures)return 1;
    printf("INPUT_DELIVERY_OK cases=8 real_core=true\n");return 0;
}
