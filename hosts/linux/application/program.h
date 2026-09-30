#ifndef POCKET_APPLICATION_PROGRAM_H
#define POCKET_APPLICATION_PROGRAM_H
#include <stddef.h>
#include <stdint.h>

/* Application-only VM. JS values/engine ABI never cross this contract. */
#define POCKET_PROGRAM_SOURCE_LIMIT (256u * 1024u)
#define POCKET_PROGRAM_TEXT_LIMIT 8192u
#define POCKET_PROGRAM_ARGUMENT_LIMIT 32u

typedef enum {
    POCKET_PROGRAM_NULL, POCKET_PROGRAM_INTEGER, POCKET_PROGRAM_BOOLEAN,
    POCKET_PROGRAM_TEXT, POCKET_PROGRAM_JSON
} PocketProgramValueKind;

typedef struct {
    PocketProgramValueKind kind;
    int64_t integer;
    const char *text;
    size_t length;
} PocketProgramValue;

/* Arguments are borrowed until return. TEXT replies are copied before the
 * next command; JSON replies are trusted native JSON, never evaluated as JS. */
typedef int (*PocketProgramCommand)(void *context, const char *operation,
    size_t count, const PocketProgramValue *arguments, PocketProgramValue *reply);
typedef struct {
    void *context;
    PocketProgramCommand command;
    size_t memory_limit;
    unsigned interrupt_budget;
    unsigned command_budget;
    unsigned pending_job_budget;
} PocketProgramConfig;

typedef struct { void *impl; } PocketProgram;
typedef struct {
    uint64_t calls, commands, jobs;
    unsigned failed, in_call;
    const char *error; /* fixed diagnostic code; no exception/input text */
} PocketProgramSnapshot;

PocketProgramConfig pocket_program_config(void);
int pocket_program_open(PocketProgram *program, const PocketProgramConfig *config,
                        const char *source, size_t source_length);
/* The app exports globalThis.PocketApplication = {start,event,tick,inspect}.
 * Method input/output is bounded JSON, not executable source. Calls are
 * synchronous; pending jobs drain under the same cooperative fuel budget. */
int pocket_program_call(PocketProgram *program, const char *method,
                        const char *input_json, size_t input_length,
                        char *output_json, size_t output_capacity, size_t *output_length);
int pocket_program_snapshot(const PocketProgram *program, PocketProgramSnapshot *out);
void pocket_program_close(PocketProgram *program);
#endif
