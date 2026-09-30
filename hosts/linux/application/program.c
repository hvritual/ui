#include "program.h"
#include "quickjs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SAFE_INTEGER INT64_C(9007199254740991)
#define MAX_SOURCE POCKET_PROGRAM_SOURCE_LIMIT
#define MAX_TEXT POCKET_PROGRAM_TEXT_LIMIT
#define MAX_ARGUMENTS POCKET_PROGRAM_ARGUMENT_LIMIT

static const char *const methods[] = {"start", "event", "tick", "inspect"};
typedef struct {
    JSRuntime *runtime;
    JSContext *context;
    JSValue application;
    PocketProgramConfig config;
    PocketProgramSnapshot snapshot;
    unsigned fuel, commands, jobs_left;
    JSValue rejections[16];
    unsigned rejection_count;
    int ready;
} Program;

static int fail(Program *p, const char *code) {
    p->snapshot.failed = 1;
    if (!p->snapshot.error) p->snapshot.error = code;
    return 0;
}
static int interrupted(JSRuntime *runtime, void *opaque) {
    Program *p = opaque;
    (void)runtime;
    if (p->snapshot.failed || !p->fuel) { fail(p, "APP_EXECUTION_BUDGET"); return 1; }
    --p->fuel;
    return 0;
}
static int valid_operation(const char *s, size_t n) {
    if (!s || !n || n > 48) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || s[i] == '_' || s[i] == '.')) return 0;
    return 1;
}
static void discard_exception(Program *p) {
    JSValue value = JS_GetException(p->context);
    JS_FreeValue(p->context, value);
}
static JSValue command(JSContext *context, JSValueConst self, int argc, JSValueConst *argv) {
    Program *p = JS_GetContextOpaque(context);
    PocketProgramValue args[MAX_ARGUMENTS] = {{0}}, result = {0};
    const char *strings[MAX_ARGUMENTS] = {0};
    const char *operation = NULL;
    size_t operation_length = 0, total = 0;
    JSValue out = JS_EXCEPTION;
    (void)self;
    if (!p || !p->snapshot.in_call || p->snapshot.failed) return JS_ThrowInternalError(context, "APP_NOT_CALLABLE");
    if (argc < 1 || argc > (int)MAX_ARGUMENTS + 1 || !JS_IsString(argv[0])) goto invalid;
    if (!p->commands) { fail(p, "APP_COMMAND_BUDGET"); goto invalid; }
    --p->commands;
    operation = JS_ToCStringLen(context, &operation_length, argv[0]);
    if (!valid_operation(operation, operation_length)) goto invalid;
    for (int i = 1; i < argc; ++i) {
        PocketProgramValue *a = &args[i-1];
        if (JS_IsNull(argv[i])) a->kind = POCKET_PROGRAM_NULL;
        else if (JS_IsBool(argv[i])) {
            a->kind = POCKET_PROGRAM_BOOLEAN;
            a->integer = JS_ToBool(context, argv[i]);
            if (a->integer < 0) goto invalid;
        } else if (JS_IsNumber(argv[i])) {
            double d;
            if (JS_ToFloat64(context, &d, argv[i]) || !isfinite(d) || trunc(d) != d ||
                d < -(double)MAX_SAFE_INTEGER || d > (double)MAX_SAFE_INTEGER) goto invalid;
            a->kind = POCKET_PROGRAM_INTEGER;
            a->integer = (int64_t)d;
        } else if (JS_IsString(argv[i])) {
            strings[i-1] = JS_ToCStringLen(context, &a->length, argv[i]);
            if (!strings[i-1] || a->length > MAX_TEXT - total || memchr(strings[i-1], 0, a->length)) goto invalid;
            a->kind = POCKET_PROGRAM_TEXT; a->text = strings[i-1]; total += a->length;
        } else goto invalid;
    }
    ++p->snapshot.commands;
    if (!p->config.command(p->config.context, operation, (size_t)argc-1, args, &result)) {
        fail(p, "APP_COMMAND_REJECTED"); goto invalid;
    }
    switch (result.kind) {
    case POCKET_PROGRAM_NULL: out = JS_NULL; break;
    case POCKET_PROGRAM_INTEGER:
        if (result.integer < -MAX_SAFE_INTEGER || result.integer > MAX_SAFE_INTEGER) goto invalid;
        out = JS_NewInt64(context, result.integer); break;
    case POCKET_PROGRAM_BOOLEAN: out = JS_NewBool(context, result.integer != 0); break;
    case POCKET_PROGRAM_TEXT:
        if (!result.text || result.length > MAX_TEXT || memchr(result.text, 0, result.length)) goto invalid;
        out = JS_NewStringLen(context, result.text, result.length); break;
    case POCKET_PROGRAM_JSON: {
        if (!result.text || result.length > MAX_TEXT || memchr(result.text, 0, result.length)) goto invalid;
        char *copy = malloc(result.length + 1);
        if (!copy) goto invalid;
        memcpy(copy, result.text, result.length); copy[result.length] = 0;
        out = JS_ParseJSON(context, copy, result.length, "native-result.json");
        free(copy); break;
    }
    default: goto invalid;
    }
    if (JS_IsException(out)) fail(p, "APP_COMMAND_RESULT");
    goto cleanup;
invalid:
    fail(p, "APP_COMMAND_ARGUMENT");
    out = JS_ThrowTypeError(context, "APP_COMMAND_INVALID");
cleanup:
    if (operation) JS_FreeCString(context, operation);
    for (unsigned i = 0; i < MAX_ARGUMENTS; ++i) if (strings[i]) JS_FreeCString(context, strings[i]);
    return out;
}
static void rejected(JSContext *context, JSValueConst promise, JSValueConst reason,
                     JS_BOOL handled, void *opaque) {
    Program *p = opaque;
    (void)reason;
    if (handled) {
        for (unsigned i = 0; i < p->rejection_count; ++i) {
            if (JS_VALUE_GET_PTR(p->rejections[i]) == JS_VALUE_GET_PTR(promise)) {
                JS_FreeValue(context, p->rejections[i]);
                p->rejections[i] = p->rejections[--p->rejection_count];
                return;
            }
        }
        return;
    }
    for (unsigned i = 0; i < p->rejection_count; ++i)
        if (JS_VALUE_GET_PTR(p->rejections[i]) == JS_VALUE_GET_PTR(promise)) return;
    if (p->rejection_count == 16) { fail(p, "APP_REJECTION_BUDGET"); return; }
    p->rejections[p->rejection_count++] = JS_DupValue(context, promise);
}
static int drain(Program *p) {
    while (JS_IsJobPending(p->runtime)) {
        JSContext *context = NULL;
        if (!p->jobs_left) return fail(p, "APP_JOB_BUDGET");
        --p->jobs_left;
        int result = JS_ExecutePendingJob(p->runtime, &context);
        if (result < 0) {
            if (context) { JSValue e = JS_GetException(context); JS_FreeValue(context, e); }
            return fail(p, "APP_PENDING_JOB");
        }
        if (!result) break;
        ++p->snapshot.jobs;
        if (p->snapshot.failed) return 0;
    }
    if (p->rejection_count) return fail(p,"APP_UNHANDLED_REJECTION");
    return !p->snapshot.failed;
}
static void begin(Program *p) {
    p->fuel = p->config.interrupt_budget;
    p->commands = p->config.command_budget;
    p->jobs_left = p->config.pending_job_budget;
    p->snapshot.in_call = 1;
}
PocketProgramConfig pocket_program_config(void) {
    return (PocketProgramConfig){.memory_limit=8u*1024u*1024u,
        .interrupt_budget=512, .command_budget=2048, .pending_job_budget=128};
}
int pocket_program_open(PocketProgram *out, const PocketProgramConfig *config,
                        const char *source, size_t length) {
    if (!out || out->impl || !config || !config->command || !source || !length || length > MAX_SOURCE ||
        memchr(source, 0, length) || config->memory_limit < 1024u*1024u || config->memory_limit > 32u*1024u*1024u ||
        !config->interrupt_budget || config->interrupt_budget > 4096 ||
        !config->command_budget || config->command_budget > 8192 ||
        !config->pending_job_budget || config->pending_job_budget > 1024) return 0;
    Program *p = calloc(1, sizeof(*p));
    if (!p) return 0;
    out->impl = p; p->application = JS_UNDEFINED; p->config = *config;
    p->runtime = JS_NewRuntime();
    if (!p->runtime) { pocket_program_close(out); return 0; }
    JS_SetMemoryLimit(p->runtime, config->memory_limit);
    JS_SetMaxStackSize(p->runtime, 256u*1024u);
    JS_SetInterruptHandler(p->runtime, interrupted, p);
    JS_SetHostPromiseRejectionTracker(p->runtime, rejected, p);
    p->context = JS_NewContext(p->runtime);
    if (!p->context) { pocket_program_close(out); return 0; }
    JS_SetContextOpaque(p->context, p);
    begin(p);
    JSValue global = JS_GetGlobalObject(p->context);
    JSValue binding = JS_NewCFunction(p->context, command, "__pocketCall", 1);
    int installed = JS_IsException(binding) ? -1 :
        JS_DefinePropertyValueStr(p->context, global, "__pocketCall", binding, 0);
    JSValue evaluated = JS_UNDEFINED;
    char *copy = malloc(length+1);
    if (!copy) { JS_FreeValue(p->context, global); p->snapshot.in_call=0; pocket_program_close(out); return 0; }
    memcpy(copy, source, length); copy[length]=0;
    if (installed >= 0) evaluated = JS_Eval(p->context, copy, length, "application.js", JS_EVAL_TYPE_GLOBAL);
    free(copy);
    int ok = installed >= 0 && !JS_IsException(evaluated) && drain(p);
    JS_FreeValue(p->context, evaluated);
    if (ok) {
        p->application = JS_GetPropertyStr(p->context, global, "PocketApplication");
        ok = JS_IsObject(p->application);
        for (unsigned i = 0; ok && i < sizeof(methods)/sizeof(methods[0]); ++i) {
            JSValue fn = JS_GetPropertyStr(p->context, p->application, methods[i]);
            ok = JS_IsFunction(p->context, fn);
            JS_FreeValue(p->context, fn);
        }
    }
    JS_FreeValue(p->context, global);
    p->snapshot.in_call = 0;
    if (!ok || p->snapshot.failed) { discard_exception(p); pocket_program_close(out); return 0; }
    p->ready = 1;
    return 1;
}
int pocket_program_call(PocketProgram *out, const char *method,
                        const char *input, size_t length, char *output,
                        size_t capacity, size_t *written) {
    Program *p = out ? out->impl : NULL;
    if (written) *written = 0;
    if (output && capacity) output[0] = 0;
    if (!p || !p->ready || p->snapshot.failed || p->snapshot.in_call || !method ||
        !input || !length || length > MAX_TEXT || memchr(input,0,length) ||
        !output || capacity < 2 || capacity > MAX_TEXT+1 || !written) return 0;
    int allowed = 0;
    for (unsigned i = 0; i < sizeof(methods)/sizeof(methods[0]); ++i) if (!strcmp(method,methods[i])) allowed=1;
    if (!allowed) return 0;
    char *copy = malloc(length+1);
    if (!copy) return fail(p, "APP_ALLOCATION");
    memcpy(copy,input,length);copy[length]=0;
    begin(p); ++p->snapshot.calls;
    JSValue argument = JS_ParseJSON(p->context,copy,length,"application-input.json");
    free(copy);
    JSValue fn = JS_UNDEFINED, value = JS_UNDEFINED, encoded = JS_UNDEFINED;
    const char *text = NULL;
    size_t size = 0;
    int ok = !JS_IsException(argument);
    if (ok) {
        fn = JS_GetPropertyStr(p->context,p->application,method);
        ok = JS_IsFunction(p->context,fn);
    }
    if (ok) {
        value = JS_Call(p->context,fn,p->application,1,&argument);
        ok = !JS_IsException(value) && drain(p);
    }
    if (ok && JS_IsObject(value)) {
        JSValue then = JS_GetPropertyStr(p->context, value, "then");
        if (JS_IsException(then)) ok = 0;
        else if (JS_IsFunction(p->context, then)) { fail(p,"APP_ASYNC_RESULT"); ok = 0; }
        JS_FreeValue(p->context, then);
    }
    if (ok) {
        encoded = JS_JSONStringify(p->context,value,JS_UNDEFINED,JS_UNDEFINED);
        ok = !JS_IsException(encoded) && JS_IsString(encoded);
    }
    if (ok) {
        text = JS_ToCStringLen(p->context,&size,encoded);
        ok = text && size < capacity && size <= MAX_TEXT && drain(p);
    }
    if (ok) { memcpy(output,text,size);output[size]=0;*written=size; }
    if (text) JS_FreeCString(p->context,text);
    JS_FreeValue(p->context,encoded);JS_FreeValue(p->context,value);
    JS_FreeValue(p->context,fn);JS_FreeValue(p->context,argument);
    p->snapshot.in_call = 0;
    if (!ok) { discard_exception(p);return fail(p,"APP_INVOCATION"); }
    return 1;
}
int pocket_program_snapshot(const PocketProgram *out, PocketProgramSnapshot *snapshot) {
    const Program *p = out ? out->impl : NULL;
    if (!p || !snapshot) return 0;
    *snapshot = p->snapshot;return 1;
}
void pocket_program_close(PocketProgram *out) {
    Program *p = out ? out->impl : NULL;
    if (!p || p->snapshot.in_call) return;
    if (p->runtime) JS_SetHostPromiseRejectionTracker(p->runtime,NULL,NULL);
    if (p->context) {
        for (unsigned i=0;i<p->rejection_count;++i) JS_FreeValue(p->context,p->rejections[i]);
    }
    if (p->context) { JS_FreeValue(p->context,p->application);JS_FreeContext(p->context); }
    if (p->runtime) JS_FreeRuntime(p->runtime);
    free(p);out->impl=NULL;
}
