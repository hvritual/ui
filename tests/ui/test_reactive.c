#include "hosts/linux/ui/reactive.h"
#include "hosts/linux/ui/external_state.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"REACTIVE_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct { int calls; } EffectTrace;

static PocketReactiveStatus sum_compute(void *context,const PocketReactiveValue *deps,
                                        uint32_t count,PocketReactiveValue *out) {
    (void)context;
    if(count!=2||deps[0].type!=POCKET_VALUE_I64||deps[1].type!=POCKET_VALUE_I64)
        return POCKET_REACTIVE_INVALID_ARGUMENT;
    *out=pocket_value_i64(deps[0].as.i64+deps[1].as.i64);
    return POCKET_REACTIVE_OK;
}
static PocketReactiveStatus double_compute(void *context,const PocketReactiveValue *deps,
                                           uint32_t count,PocketReactiveValue *out) {
    (void)context;
    if(count!=1||deps[0].type!=POCKET_VALUE_I64)return POCKET_REACTIVE_INVALID_ARGUMENT;
    *out=pocket_value_i64(deps[0].as.i64*2);
    return POCKET_REACTIVE_OK;
}
static PocketReactiveStatus trace_effect(void *context,const PocketReactiveValue *deps,
                                         uint32_t count) {
    EffectTrace *trace=context;
    if(count!=1||deps[0].type!=POCKET_VALUE_I64)return POCKET_REACTIVE_INVALID_ARGUMENT;
    trace->calls++;
    return POCKET_REACTIVE_OK;
}
int main(void) {
    PocketReactiveRuntime rt={0};
    PocketReactiveConfig cfg={.node_capacity=32,.effect_capacity=8,.binding_capacity=0,.default_flush_budget=64};
    CHECK(pocket_reactive_init(&rt,&cfg)==POCKET_REACTIVE_OK);
    PocketReactiveHandle a,b,total,doubled;
    CHECK(pocket_reactive_signal(&rt,pocket_value_i64(1),&a)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_signal(&rt,pocket_value_i64(2),&b)==POCKET_REACTIVE_OK);
    PocketReactiveHandle sum_deps[]={a,b};
    CHECK(pocket_reactive_computed(&rt,POCKET_VALUE_I64,sum_deps,2,sum_compute,NULL,&total)==POCKET_REACTIVE_OK);
    EffectTrace trace={0};uint32_t effect_id=0;
    CHECK(pocket_reactive_effect(&rt,&total,1,trace_effect,&trace,&effect_id)==POCKET_REACTIVE_OK);
    PocketReactiveFlushStats stats;
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK);
    CHECK(trace.calls==1&&stats.computed_runs==1&&stats.effect_runs==1);
    PocketReactiveValue value;
    CHECK(pocket_reactive_get(&rt,total,&value)==POCKET_REACTIVE_OK&&value.as.i64==3);

    CHECK(pocket_reactive_batch_begin(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&rt,a,pocket_value_i64(5))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&rt,b,pocket_value_i64(7))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_BUSY);
    CHECK(pocket_reactive_batch_end(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK&&trace.calls==2);
    CHECK(pocket_reactive_get(&rt,total,&value)==POCKET_REACTIVE_OK&&value.as.i64==12);

    CHECK(pocket_reactive_transaction_begin(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&rt,a,pocket_value_i64(20))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_get(&rt,a,&value)==POCKET_REACTIVE_OK&&value.as.i64==5);
    CHECK(pocket_reactive_transaction_rollback(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK&&trace.calls==2);

    CHECK(pocket_reactive_transaction_begin(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&rt,a,pocket_value_i64(20))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_set(&rt,b,pocket_value_i64(30))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_transaction_commit(&rt)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK&&trace.calls==3);
    CHECK(pocket_reactive_get(&rt,total,&value)==POCKET_REACTIVE_OK&&value.as.i64==50);

    PocketReactiveHandle doubled_deps[]={total};
    CHECK(pocket_reactive_computed(&rt,POCKET_VALUE_I64,doubled_deps,1,double_compute,NULL,&doubled)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_get(&rt,doubled,&value)==POCKET_REACTIVE_OK&&value.as.i64==100);
    CHECK(pocket_reactive_rewire_computed(&rt,total,&doubled,1)==POCKET_REACTIVE_CYCLE);

    PocketExternalStateBinding storage[2];
    PocketExternalStateAdapter external;
    CHECK(pocket_external_state_init(&external,&rt,storage,2)==POCKET_REACTIVE_OK);
    CHECK(pocket_external_state_bind(&external,77,a)==POCKET_REACTIVE_OK);
    CHECK(pocket_external_state_ingest(&external,77,pocket_value_i64(41))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_get(&rt,total,&value)==POCKET_REACTIVE_OK&&value.as.i64==71);
    CHECK(pocket_external_state_ingest(&external,999,pocket_value_i64(1))==POCKET_REACTIVE_STALE_HANDLE);

    CHECK(pocket_reactive_remove_effect(&rt,effect_id)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_destroy(&rt,doubled)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_destroy(&rt,total)==POCKET_REACTIVE_OK);
    pocket_reactive_dispose(&rt);
    puts("REACTIVE_OK");
    return 0;
}
