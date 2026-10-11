#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/ipc/service.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A real, independent socket provider for functional acceptance. The counter
 * is synthetic business data. No device/actuator API is linked. Deduplication
 * lasts for this process only: a production authority needs a durable journal
 * coupled to its action, not this in-memory example. Entries are never evicted;
 * capacity exhaustion rejects new commands before changing the counter. */
#define FIXTURE_OPERATIONS 16U
typedef struct {
    char key[POCKET_IPC_NAME_MAX + 1U];
    int delta;
    char receipt[192];
} Operation;
typedef struct {
    PocketService service;
    const char *event, *state;
    int value;
    unsigned operation_count, event_failures;
    Operation operations[FIXTURE_OPERATIONS];
} Fixture;

static volatile sig_atomic_t stopping;
static void stop(int signum) { (void)signum; stopping = 1; }
static const char *space(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

/* Deliberately small business schema: exactly {"delta":integer}, -100..100.
 * Unknown properties, numeric coercion, repeated keys and trailing data fail. */
static int parse_delta(const char *payload, int *delta) {
    const char *p = space(payload);
    if (*p++ != '{') return 0;
    p = space(p);
    if (strncmp(p, "\"delta\"", 7)) return 0;
    p = space(p + 7);
    if (*p++ != ':') return 0;
    p = space(p);
    const char *start = p;
    if (*p == '-') ++p;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9')) return 0;
    while (*p >= '0' && *p <= '9') ++p;
    if ((size_t)(p - start) > 4U) return 0;
    char number[8];
    memcpy(number, start, (size_t)(p - start));
    number[p - start] = 0;
    long value = strtol(number, NULL, 10);
    if (value < -100 || value > 100) return 0;
    p = space(p);
    if (*p++ != '}' || *space(p)) return 0;
    *delta = (int)value;
    return 1;
}

static void error(PocketServiceReply *reply, PocketIpcStatus status, const char *payload) {
    if (!pocket_service_error(reply, status, payload, strlen(payload))) abort();
}

static void query(void *opaque, const PocketServiceRequest *request, PocketServiceReply *reply) {
    Fixture *fixture = opaque;
    const char *p = space(request->payload);
    if (strcmp(p, "{}") && strcmp(p, "null")) {
        error(reply, POCKET_IPC_REJECTED, "{\"code\":\"INVALID_QUERY\"}");
        return;
    }
    char payload[192];
    int n = snprintf(payload, sizeof(payload),
                     "{\"value\":%d,\"operations\":%u,\"eventFailures\":%u}",
                     fixture->value, fixture->operation_count, fixture->event_failures);
    if (n < 0 || (size_t)n >= sizeof(payload) || !pocket_service_reply(reply, payload, (size_t)n)) abort();
}

static void command(void *opaque, const PocketServiceRequest *request, PocketServiceReply *reply) {
    Fixture *fixture = opaque;
    int delta = 0;
    if (!request->idempotency_key[0] || !parse_delta(request->payload, &delta)) {
        error(reply, POCKET_IPC_REJECTED, "{\"code\":\"INVALID_COMMAND\"}");
        return;
    }
    for (unsigned i = 0; i < fixture->operation_count; ++i) {
        Operation *operation = &fixture->operations[i];
        if (strcmp(operation->key, request->idempotency_key)) continue;
        if (operation->delta != delta) {
            error(reply, POCKET_IPC_REJECTED, "{\"code\":\"IDEMPOTENCY_CONFLICT\"}");
            return;
        }
        if (!pocket_service_reply(reply, operation->receipt, strlen(operation->receipt))) abort();
        return;
    }
    if (fixture->operation_count == FIXTURE_OPERATIONS) {
        error(reply, POCKET_IPC_BUSY, "{\"code\":\"OPERATION_JOURNAL_FULL\"}");
        return;
    }
    int value = fixture->value + delta;
    if (value < -1000000 || value > 1000000) {
        error(reply, POCKET_IPC_REJECTED, "{\"code\":\"COUNTER_LIMIT\"}");
        return;
    }
    PocketIpcFilter metadata = {{0}, {0}};
    strcpy(metadata.key, "parity");
    strcpy(metadata.value, value % 2 ? "odd" : "even");
    char state[192];
    int n = snprintf(state, sizeof(state), "{\"value\":%d,\"operations\":%u}",
                     value, fixture->operation_count + 1U);
    if (n < 0 || (size_t)n >= sizeof(state)) abort();
    PocketIpcStatus status = pocket_service_state_put(&fixture->service, fixture->state, 1,
                                                     &metadata, state, (size_t)n);
    if (status != POCKET_IPC_OK) {
        error(reply, status, "{\"code\":\"STATE_BACKPRESSURE\"}");
        return;
    }
    Operation *operation = &fixture->operations[fixture->operation_count];
    strcpy(operation->key, request->idempotency_key);
    operation->delta = delta;
    fixture->value = value;
    ++fixture->operation_count;
    n = snprintf(operation->receipt, sizeof(operation->receipt),
                 "{\"status\":\"completed\",\"operationId\":%u,\"value\":%d}",
                 fixture->operation_count, fixture->value);
    if (n < 0 || (size_t)n >= sizeof(operation->receipt)) abort();
    if (pocket_service_publish(&fixture->service, fixture->event, &metadata,
                               operation->receipt, (size_t)n) != POCKET_IPC_OK)
        ++fixture->event_failures;
    if (!pocket_service_reply(reply, operation->receipt, (size_t)n)) abort();
}

int main(int argc, char **argv) {
    const char *socket_path = NULL, *service_name = NULL;
    const char *query_name = "get", *command_name = "add", *event_name = "changed", *state_name = "counter";
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 2;
        if (!strcmp(argv[i], "--socket")) socket_path = argv[i + 1];
        else if (!strcmp(argv[i], "--service")) service_name = argv[i + 1];
        else if (!strcmp(argv[i], "--query")) query_name = argv[i + 1];
        else if (!strcmp(argv[i], "--command")) command_name = argv[i + 1];
        else if (!strcmp(argv[i], "--event")) event_name = argv[i + 1];
        else if (!strcmp(argv[i], "--state")) state_name = argv[i + 1];
        else return 2;
    }
    if (!socket_path || !service_name) return 2;
    Fixture fixture;
    memset(&fixture, 0, sizeof(fixture));
    fixture.event = event_name;
    fixture.state = state_name;
    PocketServiceMember members[] = {
        {POCKET_SERVICE_QUERY, query_name, query, &fixture},
        {POCKET_SERVICE_COMMAND, command_name, command, &fixture},
        {POCKET_SERVICE_EVENT, event_name, NULL, NULL},
        {POCKET_SERVICE_STATE, state_name, NULL, NULL}
    };
    PocketServiceConfig config = pocket_service_config();
    config.socket_path = socket_path;
    config.service = service_name;
    config.members = members;
    config.member_count = sizeof(members) / sizeof(members[0]);
    PocketIpcStatus status = pocket_service_open(&fixture.service, &config);
    if (status != POCKET_IPC_OK) {
        fprintf(stderr, "SERVICE_OPEN_FAILED %s\n", pocket_ipc_status_name(status));
        return 3;
    }
    PocketIpcFilter metadata = {{0}, {0}};
    strcpy(metadata.key, "parity");
    strcpy(metadata.value, "even");
    static const char initial[] = "{\"value\":0,\"operations\":0}";
    if (pocket_service_state_put(&fixture.service, state_name, 1, &metadata,
                                initial, sizeof(initial) - 1U) != POCKET_IPC_OK) return 4;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) return 5;
    uint64_t announced_epoch = 0, announced_broker = 0, retry_at = 0;
    while (!stopping) {
        uint64_t now = pocket_ipc_monotonic_ms();
        if (retry_at && now >= retry_at) {
            status = pocket_service_reconnect(&fixture.service);
            retry_at = status == POCKET_IPC_OK ? 0 : now + 100U;
        }
        if (!retry_at) {
            status = pocket_service_pump(&fixture.service, 16);
            if (status != POCKET_IPC_OK) {
                fprintf(stderr, "SERVICE_RECONNECT status=%s\n", pocket_ipc_status_name(status));
                retry_at = now + 100U;
            }
        }
        PocketServiceSnapshot snapshot;
        if (pocket_service_snapshot(&fixture.service, &snapshot) && snapshot.ready &&
            (snapshot.epoch != announced_epoch || snapshot.broker_epoch != announced_broker)) {
            printf("SERVICE_READY service=%s epoch=%" PRIu64 " broker=%" PRIu64 " pid=%ld\n",
                   service_name, snapshot.epoch, snapshot.broker_epoch, (long)getpid());
            fflush(stdout);
            announced_epoch = snapshot.epoch;
            announced_broker = snapshot.broker_epoch;
        }
        if (poll(NULL, 0, 2) < 0 && errno != EINTR) break;
    }
    pocket_service_close(&fixture.service);
    puts("SERVICE_STOPPED synthetic-counter-only; no-device-admission; process-local-dedup");
    return 0;
}
