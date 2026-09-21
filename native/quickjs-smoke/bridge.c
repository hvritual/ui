/* Keep JSValue and its platform-dependent representation inside C.
 * This is a toolchain test bridge, NOT the PocketJS UI runtime or host. */
#include "quickjs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4, "this smoke bridge must be built for 32-bit ARM");

static int copy_text(char *output, size_t capacity, const char *text) {
    if (!output || capacity == 0) return 1;
    int count = snprintf(output, capacity, "%s", text ? text : "unknown exception");
    return count < 0 || (size_t)count >= capacity;
}

static JSValue native_add(JSContext *ctx, JSValueConst self, int argc,
                          JSValueConst *argv) {
    (void)self;
    int32_t a, b;
    if (argc != 2) return JS_ThrowTypeError(ctx, "nativeAdd needs two arguments");
    if (JS_ToInt32(ctx, &a, argv[0]) || JS_ToInt32(ctx, &b, argv[1]))
        return JS_EXCEPTION;
    return JS_NewInt64(ctx, (int64_t)a + (int64_t)b);
}

static int interrupt(JSRuntime *runtime, void *opaque) {
    (void)runtime;
    unsigned *budget = opaque;
    if (*budget == 0) return 1;
    --*budget;
    return 0;
}

static void exception_text(JSContext *ctx, char *error, size_t capacity) {
    JSValue exception = JS_GetException(ctx);
    const char *message = JS_ToCString(ctx, exception);
    copy_text(error, capacity, message);
    if (message) JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
}

unsigned ui_js_value_size(void) { return (unsigned)sizeof(JSValue); }

/* Returns zero only when evaluating, draining jobs and reading __result succeed.
 * The caller owns both output buffers. A fresh runtime bounds each test case. */
int ui_js_evaluate(const char *source, char *output, size_t output_capacity,
                   char *error, size_t error_capacity) {
    if (!source || !output || !error || !output_capacity || !error_capacity) return 1;
    output[0] = error[0] = '\0';
    JSRuntime *runtime = JS_NewRuntime();
    if (!runtime) {
        copy_text(error, error_capacity, "runtime allocation failed");
        return 1;
    }
    JS_SetMemoryLimit(runtime, 16u * 1024u * 1024u);
    JS_SetMaxStackSize(runtime, 512u * 1024u);
    unsigned budget = 500;
    JS_SetInterruptHandler(runtime, interrupt, &budget);
    JSContext *ctx = JS_NewContext(runtime);
    if (!ctx) {
        copy_text(error, error_capacity, "context allocation failed");
        JS_FreeRuntime(runtime);
        return 1;
    }
    int status = 1;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue result = JS_UNDEFINED;
    if (JS_SetPropertyStr(ctx, global, "nativeAdd",
            JS_NewCFunction(ctx, native_add, "nativeAdd", 2)) < 0) goto js_error;
    result = JS_Eval(ctx, source, strlen(source), "toolchain-smoke.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) goto js_error;
    JS_FreeValue(ctx, result);
    result = JS_UNDEFINED;
    for (unsigned jobs = 0;; ++jobs) {
        if (jobs == 256) {
            copy_text(error, error_capacity, "pending job budget exceeded");
            goto cleanup;
        }
        JSContext *job_context = NULL;
        int pending = JS_ExecutePendingJob(runtime, &job_context);
        if (pending < 0) {
            JS_SetInterruptHandler(runtime, NULL, NULL);
            exception_text(job_context ? job_context : ctx, error, error_capacity);
            goto cleanup;
        }
        if (!pending) break;
    }
    result = JS_GetPropertyStr(ctx, global, "__result");
    if (JS_IsException(result)) goto js_error;
    const char *text = JS_ToCString(ctx, result);
    if (!text) goto js_error;
    status = copy_text(output, output_capacity, text);
    JS_FreeCString(ctx, text);
    if (status) copy_text(error, error_capacity, "output buffer too small");
    goto cleanup;
js_error:
    JS_SetInterruptHandler(runtime, NULL, NULL);
    exception_text(ctx, error, error_capacity);
cleanup:
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    return status;
}
