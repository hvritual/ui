#include "hosts/linux/application/program.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"PROGRAM_ASSERT line=%u\n",(unsigned)__LINE__); return 0; } } while (0)
#define APP(body) "globalThis.PocketApplication={start(x){" body "},event(x){return x;},tick(x){return x;},inspect(){return null;}};"
typedef struct { unsigned calls; PocketProgram *program; } Context;
static int command(void *opaque, const char *name, size_t count,
                   const PocketProgramValue *args, PocketProgramValue *reply) {
    Context *c=opaque; ++c->calls;
    if (!strcmp(name,"echo") && count==1) { *reply=args[0];return 1; }
    if (!strcmp(name,"sum") && count==2 && args[0].kind==POCKET_PROGRAM_INTEGER && args[1].kind==POCKET_PROGRAM_INTEGER &&
        args[0].integer>=-10000 && args[0].integer<=10000 && args[1].integer>=-10000 && args[1].integer<=10000) {
        reply->kind=POCKET_PROGRAM_INTEGER;reply->integer=args[0].integer+args[1].integer;return 1;
    }
    if (!strcmp(name,"json") && count==0) {
        static const char data[]="{\"value\":7}";
        reply->kind=POCKET_PROGRAM_JSON;reply->text=data;reply->length=sizeof(data)-1;return 1;
    }
    if (!strcmp(name,"reenter") && count==0 && c->program) {
        char out[32];size_t n;
        reply->kind=POCKET_PROGRAM_BOOLEAN;
        reply->integer=!pocket_program_call(c->program,"inspect","null",4,out,sizeof(out),&n);
        pocket_program_close(c->program); /* must not destroy a currently executing VM */
        return c->program->impl!=NULL;
    }
    return 0;
}
static PocketProgramConfig config(Context *c) {
    PocketProgramConfig k=pocket_program_config();k.command=command;k.context=c;return k;
}
static int call(PocketProgram *p,const char *method,const char *input,const char *expected) {
    char out[POCKET_PROGRAM_TEXT_LIMIT+1];size_t n;
    REQUIRE(pocket_program_call(p,method,input,strlen(input),out,sizeof(out),&n));
    REQUIRE(n==strlen(expected)&&!strcmp(out,expected));return 1;
}
static int basic(void) {
    PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
    static const char source[]=APP("return {number:__pocketCall('sum',x.a,x.b),text:__pocketCall('echo',x.text),native:__pocketCall('json')};");
    REQUIRE(pocket_program_open(&p,&k,source,sizeof(source)-1));
    REQUIRE(call(&p,"start","{\"a\":4,\"b\":9,\"text\":\"你好 café\"}","{\"number\":13,\"text\":\"你好 café\",\"native\":{\"value\":7}}"));
    REQUIRE(call(&p,"event","true","true"));
    REQUIRE(call(&p,"tick","17","17"));
    REQUIRE(call(&p,"inspect","null","null"));
    PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&!s.failed&&s.commands==3&&s.calls==4&&!s.in_call);
    pocket_program_close(&p);pocket_program_close(&p);REQUIRE(!p.impl);return 1;
}
static int isolated(void) {
    const char *source[]={
        "let state=1;globalThis.PocketApplication={start(){return ++state;},event(){return ++state;},tick(){return state;},inspect(){return state;}};",
        "let state=100;globalThis.PocketApplication={start(){state*=2;return state;},event(){return --state;},tick(){return state;},inspect(){return state;}};"
    };
    PocketProgram a={0},b={0};Context ca={0,&a},cb={0,&b};PocketProgramConfig ka=config(&ca),kb=config(&cb);
    REQUIRE(pocket_program_open(&a,&ka,source[0],strlen(source[0]))&&pocket_program_open(&b,&kb,source[1],strlen(source[1])));
    REQUIRE(call(&a,"start","null","2")&&call(&b,"start","null","200"));
    REQUIRE(call(&a,"event","null","3")&&call(&b,"event","null","199"));
    REQUIRE(call(&a,"inspect","null","3")&&call(&b,"inspect","null","199"));
    pocket_program_close(&a);REQUIRE(call(&b,"tick","null","199"));pocket_program_close(&b);return 1;
}
static int opening(void) {
    const char *bad[]={"", "throw Error('SYNTHETIC_PRIVATE_EXCEPTION')", "globalThis.PocketApplication={};",
        "globalThis.PocketApplication={start:1,event(){},tick(){},inspect(){}};", "while(true){}", "let x="};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
        PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);k.interrupt_budget=1;
        REQUIRE(!pocket_program_open(&p,&k,bad[i],strlen(bad[i]))&&!p.impl);
    }
    PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
    static const char src[]=APP("return null;");
    k.memory_limit=1;REQUIRE(!pocket_program_open(&p,&k,src,sizeof(src)-1));
    k=config(&c);k.command_budget=0;REQUIRE(!pocket_program_open(&p,&k,src,sizeof(src)-1));
    k=config(&c);REQUIRE(!pocket_program_open(&p,&k,"abc\0def",7));
    REQUIRE(pocket_program_open(&p,&k,src,sizeof(src)-1));
    REQUIRE(!pocket_program_open(&p,&k,src,sizeof(src)-1));pocket_program_close(&p);return 1;
}
static int native_validation(void) {
    const char *calls[]={"__pocketCall('echo',{})", "__pocketCall('echo',1.5)", "__pocketCall('echo',NaN)",
        "__pocketCall('echo',9007199254740992)", "__pocketCall('echo',1n)", "__pocketCall('echo','a\\0b')",
        "__pocketCall('echo','x'.repeat(8193))", "__pocketCall('../shell','x')", "__pocketCall('shell','x')",
        "__pocketCall('echo',...Array(33).fill(1))", "__pocketCall()"};
    for(unsigned i=0;i<sizeof(calls)/sizeof(calls[0]);++i){
        char src[2048],out[64];size_t n;
        snprintf(src,sizeof(src),"globalThis.PocketApplication={start(){try{return %s;}catch(e){return 'caught';}},event(x){return x;},tick(x){return x;},inspect(){return null;}};",calls[i]);
        PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
        REQUIRE(pocket_program_open(&p,&k,src,strlen(src)));
        REQUIRE(!pocket_program_call(&p,"start","null",4,out,sizeof(out),&n));
        REQUIRE(n==0&&out[0]==0);
        PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&s.failed&&s.error&&!strstr(s.error,"SYNTHETIC"));
        REQUIRE(!pocket_program_call(&p,"inspect","null",4,out,sizeof(out),&n));pocket_program_close(&p);
    }
    return 1;
}
static int invocation(void) {
    static const char src[]=APP("return x;");
    PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);char out[64];size_t n;
    REQUIRE(pocket_program_open(&p,&k,src,sizeof(src)-1));
    REQUIRE(!pocket_program_call(&p,"constructor","null",4,out,sizeof(out),&n));
    REQUIRE(!pocket_program_call(&p,"start","null\0hidden",11,out,sizeof(out),&n));
    REQUIRE(call(&p,"event","{\"value\":\"x\\\";throw 1;//\"}","{\"value\":\"x\\\";throw 1;//\"}"));
    REQUIRE(!pocket_program_call(&p,"start","{bad",4,out,sizeof(out),&n));
    PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&s.failed);pocket_program_close(&p);
    static const char wide[]=APP("return 'x'.repeat(65);");
    REQUIRE(pocket_program_open(&p,&k,wide,sizeof(wide)-1));
    REQUIRE(!pocket_program_call(&p,"start","null",4,out,sizeof(out),&n)&&n==0&&out[0]==0);
    pocket_program_close(&p);return 1;
}
static int limits(void) {
    const char *sources[]={APP("while(true){}"),APP("for(let i=0;i<20;i++)__pocketCall('echo',i);return null;"),
        APP("let q=()=>Promise.resolve().then(q);q();return null;"),APP("return new Uint8Array(16*1024*1024);"),
        APP("return {toJSON(){let q=()=>Promise.resolve().then(q);q();return 1;}};")};
    const char *codes[]={"APP_EXECUTION_BUDGET","APP_COMMAND_BUDGET","APP_JOB_BUDGET","APP_INVOCATION","APP_JOB_BUDGET"};
    for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);++i){
        PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);char out[64];size_t n;
        k.interrupt_budget=10;k.command_budget=4;k.pending_job_budget=8;k.memory_limit=1024*1024;
        REQUIRE(pocket_program_open(&p,&k,sources[i],strlen(sources[i])));
        REQUIRE(!pocket_program_call(&p,"start","null",4,out,sizeof(out),&n));
        PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&s.failed&&s.error&&!strcmp(s.error,codes[i]));
        pocket_program_close(&p);
    }
    return 1;
}
static int jobs_and_reentry(void) {
    static const char source[]="let state=0;globalThis.PocketApplication={start(){Promise.resolve().then(()=>state=__pocketCall('sum',2,3));return __pocketCall('reenter');},event(x){return x;},tick(){return state;},inspect(){return state;}};";
    PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
    REQUIRE(pocket_program_open(&p,&k,source,sizeof(source)-1));
    REQUIRE(call(&p,"start","null","true"));REQUIRE(call(&p,"tick","null","5"));
    PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&s.jobs>0&&!s.failed);pocket_program_close(&p);return 1;
}
static int promise_policy(void) {
    const char *bad[] = {
        APP("return Promise.resolve(1);"),
        APP("return {then(){return 1;}};"),
        APP("Promise.reject('SYNTHETIC_PRIVATE_REJECTION');return null;"),
        APP("Promise.resolve().then(()=>{throw Error('SYNTHETIC_PRIVATE_JOB');});return null;")
    };
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);char out[64];size_t n;
        REQUIRE(pocket_program_open(&p,&k,bad[i],strlen(bad[i])));
        REQUIRE(!pocket_program_call(&p,"start","null",4,out,sizeof(out),&n));
        PocketProgramSnapshot s;REQUIRE(pocket_program_snapshot(&p,&s)&&s.failed&&s.error&&!strstr(s.error,"SYNTHETIC"));
        pocket_program_close(&p);
    }
    static const char handled[]=APP("Promise.reject(1).catch(()=>__pocketCall('echo',7));return 8;");
    PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
    REQUIRE(pocket_program_open(&p,&k,handled,sizeof(handled)-1));
    REQUIRE(call(&p,"start","null","8"));REQUIRE(c.calls==1);pocket_program_close(&p);
    return 1;
}
static int no_host_io_and_cycles(void) {
    static const char source[]=APP("return [typeof process,typeof require,typeof std,typeof os,typeof fetch,typeof console];");
    for(unsigned i=0;i<100;i++){
        PocketProgram p={0};Context c={0,&p};PocketProgramConfig k=config(&c);
        REQUIRE(pocket_program_open(&p,&k,source,sizeof(source)-1));
        REQUIRE(call(&p,"start","null","[\"undefined\",\"undefined\",\"undefined\",\"undefined\",\"undefined\",\"undefined\"]"));
        pocket_program_close(&p);REQUIRE(!p.impl);
    }
    return 1;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--intentional-failure")){fputs("INTENTIONAL_PROGRAM_ASSERTION_FAILURE\n",stderr);return 1;}
    if(argc!=1)return 2;
    int (*cases[])(void)={basic,isolated,opening,native_validation,invocation,limits,jobs_and_reentry,promise_policy,no_host_io_and_cycles};
    const char *names[]={"typed-commands-json","isolated-app-programs","open-rejection","native-argument-rejection","invocation-json-output","cooperative-budgets","jobs-reentry","promise-result-rejection-policy","no-host-io-lifecycle"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){if(!cases[i]())return 1;printf("PROGRAM_CASE_PASS %s\n",names[i]);}
    puts("PROGRAM_OK real-quickjs isolated-program-core only; no-renderer-no-board-admission");return 0;
}
