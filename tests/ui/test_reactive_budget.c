#include "hosts/linux/ui/reactive.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"REACTIVE_BUDGET_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)

typedef struct {
    PocketReactiveRuntime *runtime;
    PocketReactiveHandle signal;
    int runs;
} Feedback;

static PocketReactiveStatus feedback_effect(void *context,const PocketReactiveValue *deps,uint32_t count) {
    Feedback *f=context;
    if(count!=1||deps[0].type!=POCKET_VALUE_I64)return POCKET_REACTIVE_INVALID_ARGUMENT;
    f->runs++;
    return pocket_reactive_set(f->runtime,f->signal,pocket_value_i64(deps[0].as.i64+1));
}
static PocketReactiveStatus identity_compute(void *context,const PocketReactiveValue *deps,
                                             uint32_t count,PocketReactiveValue *out) {
    (void)context;
    if(count!=1)return POCKET_REACTIVE_INVALID_ARGUMENT;
    *out=deps[0];
    return POCKET_REACTIVE_OK;
}
int main(void) {
    PocketReactiveRuntime rt={0};
    PocketReactiveConfig cfg={.node_capacity=16,.effect_capacity=4,.binding_capacity=0,.default_flush_budget=64};
    CHECK(pocket_reactive_init(&rt,&cfg)==POCKET_REACTIVE_OK);
    PocketReactiveHandle source,derived;
    CHECK(pocket_reactive_signal(&rt,pocket_value_i64(0),&source)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_computed(&rt,POCKET_VALUE_I64,&source,1,identity_compute,NULL,&derived)==POCKET_REACTIVE_OK);
    PocketReactiveFlushStats stats;
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK);
    for(int i=1;i<=1000;i++)
        CHECK(pocket_reactive_set(&rt,source,pocket_value_i64(i))==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,0,&stats)==POCKET_REACTIVE_OK);
    CHECK(stats.computed_runs==1);
    PocketReactiveValue value;
    CHECK(pocket_reactive_get(&rt,derived,&value)==POCKET_REACTIVE_OK&&value.as.i64==1000);

    Feedback feedback={&rt,source,0};uint32_t effect_id=0;
    CHECK(pocket_reactive_effect(&rt,&source,1,feedback_effect,&feedback,&effect_id)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,5,&stats)==POCKET_REACTIVE_BUDGET_EXHAUSTED);
    CHECK(stats.work_items==5&&feedback.runs>0);
    CHECK(pocket_reactive_remove_effect(&rt,effect_id)==POCKET_REACTIVE_OK);
    CHECK(pocket_reactive_flush(&rt,64,&stats)==POCKET_REACTIVE_OK);

    pocket_reactive_dispose(&rt);
    puts("REACTIVE_BUDGET_OK");
    return 0;
}
