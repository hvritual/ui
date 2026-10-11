#define _POSIX_C_SOURCE 200809L
#include "hosts/linux/ipc/broker.h"
#include "hosts/linux/ipc/client.h"
#include "hosts/linux/ipc/service.h"
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "IPC_SERVICE_ASSERT line=%u expression=%s\n", (unsigned)__LINE__, #x); goto fail; } } while (0)
#define WAIT_MS 3000U

typedef struct {
    PocketService *service;
    const char *event;
    unsigned calls, burst;
    PocketIpcStatus burst_status, reentry_status;
    char key[POCKET_IPC_NAME_MAX + 1U];
} Handler;

typedef struct {
    char directory[80], path[POCKET_IPC_SOCKET_PATH_MAX + 1U];
    PocketIpcBroker broker;
    PocketIpcClient client;
    PocketService services[3];
    Handler handlers[3];
} Harness;

static void echo(void *opaque, const PocketServiceRequest *request, PocketServiceReply *reply) {
    Handler *handler = opaque;
    ++handler->calls;
    handler->reentry_status = pocket_service_pump(handler->service, 1);
    memcpy(handler->key, request->idempotency_key, strlen(request->idempotency_key) + 1U);
    if (!strcmp(request->payload, "burst")) {
        for (unsigned i = 0; i < POCKET_IPC_TX_CAPACITY + 1U; ++i) {
            handler->burst_status = pocket_service_publish(handler->service, handler->event, NULL, "event", 5);
            if (handler->burst_status != POCKET_IPC_OK) break;
            ++handler->burst;
        }
    }
    if (!strcmp(request->payload, "close")) pocket_service_close(handler->service);
    if (!strcmp(request->payload, "reject")) {
        if (!pocket_service_error(reply, POCKET_IPC_REJECTED, "business-rejection", 18)) abort();
    } else if (!pocket_service_reply(reply, request->payload, request->payload_length)) abort();
}

static int open_broker(Harness *h) {
    PocketIpcBrokerPolicy policy = {h->path, getuid(), getuid()};
    PocketIpcStatus status = pocket_ipc_broker_open(&h->broker, &policy);
    if (status != POCKET_IPC_OK) fprintf(stderr, "IPC_SERVICE_BROKER_OPEN status=%s path=%s\n", pocket_ipc_status_name(status), h->path);
    return status == POCKET_IPC_OK;
}

static int open_client(Harness *h) {
    PocketIpcClientConfig config = {h->path, "provider-tests", POCKET_IPC_ALLOW_ALL};
    PocketIpcStatus status = pocket_ipc_client_open(&h->client, &config);
    if (status != POCKET_IPC_OK) fprintf(stderr, "IPC_SERVICE_CLIENT_OPEN status=%s\n", pocket_ipc_status_name(status));
    return status == POCKET_IPC_OK;
}

static int begin(Harness *h) {
    memset(h, 0, sizeof(*h));
    strcpy(h->directory, "/tmp/pocket-service-XXXXXX");
    if (!mkdtemp(h->directory)) return 0;
    int n = snprintf(h->path, sizeof(h->path), "%s/bus", h->directory);
    return n > 0 && (size_t)n < sizeof(h->path) && open_broker(h) && open_client(h);
}

static int open_service(Harness *h, unsigned index, const char *service, int alternative) {
    Handler *handler = &h->handlers[index];
    handler->service = &h->services[index];
    handler->event = alternative ? "notice" : "changed";
    PocketServiceMember members[] = {
        {POCKET_SERVICE_QUERY, alternative ? "inspect" : "get", echo, handler},
        {POCKET_SERVICE_COMMAND, alternative ? "increase" : "add", echo, handler},
        {POCKET_SERVICE_EVENT, handler->event, NULL, NULL},
        {POCKET_SERVICE_STATE, alternative ? "values" : "objects", NULL, NULL}
    };
    PocketServiceConfig config = pocket_service_config();
    config.socket_path = h->path;
    config.service = service;
    config.members = members;
    config.member_count = sizeof(members) / sizeof(members[0]);
    return pocket_service_open(&h->services[index], &config) == POCKET_IPC_OK;
}

/* Every participant uses the production stream transport and independent
 * connection state. Keeping owners in one test process also runs under qemu;
 * the separate service_fixture executable is exercised by framework acceptance. */
static void advance(Harness *h, unsigned rounds) {
    for (unsigned round = 0; round < rounds; ++round) {
        uint64_t now = pocket_ipc_monotonic_ms();
        if (h->broker.impl) (void)pocket_ipc_broker_step(&h->broker, now, 32);
        for (unsigned i = 0; i < 3; ++i)
            if (h->services[i].impl) (void)pocket_service_pump(&h->services[i], 16);
        if (h->client.impl) (void)pocket_ipc_client_step(&h->client, now, 32);
        (void)poll(NULL, 0, 1);
    }
}

static int wait_ready(Harness *h, unsigned index) {
    uint64_t until = pocket_ipc_monotonic_ms() + WAIT_MS;
    while (pocket_ipc_monotonic_ms() < until) {
        advance(h, 1);
        PocketServiceSnapshot service;
        PocketIpcClientStats client;
        if (pocket_service_snapshot(&h->services[index], &service) && service.ready &&
            pocket_ipc_client_stats(&h->client, &client) == POCKET_IPC_OK && client.ready) return 1;
    }
    return 0;
}

static int message(Harness *h, PocketIpcKind kind, uint64_t id, PocketIpcMessage *result) {
    uint64_t until = pocket_ipc_monotonic_ms() + WAIT_MS;
    while (pocket_ipc_monotonic_ms() < until) {
        PocketIpcMessage next;
        while (pocket_ipc_client_next(&h->client, &next) == POCKET_IPC_OK) {
            if (next.kind == POCKET_IPC_SUBSCRIBED) continue;
            if ((!kind || next.kind == kind) && (!id || next.request_id == id)) {
                *result = next;
                return 1;
            }
            fprintf(stderr, "IPC_SERVICE_UNEXPECTED kind=%u want=%u id=%" PRIu64 " want_id=%" PRIu64 " status=%s\n",
                    (unsigned)next.kind, (unsigned)kind, next.request_id, id, pocket_ipc_status_name(next.status));
            return 0;
        }
        advance(h, 1);
    }
    fprintf(stderr, "IPC_SERVICE_WAIT_TIMEOUT kind=%u id=%" PRIu64 "\n", (unsigned)kind, id);
    return 0;
}

static int subscribe(Harness *h, const char *service, const char *member,
                     const PocketIpcFilter *filter, uint64_t *id) {
    return pocket_ipc_client_subscribe(&h->client, POCKET_IPC_WATCH, service, member, filter,
                                        WAIT_MS, pocket_ipc_monotonic_ms(), id) == POCKET_IPC_OK;
}

static int empty_snapshot(Harness *h, uint64_t id, uint64_t *revision) {
    PocketIpcMessage begin_message, end_message;
    if (!message(h, POCKET_IPC_SNAPSHOT, id, &begin_message) ||
        begin_message.flags != POCKET_IPC_SNAPSHOT_BEGIN || begin_message.status != POCKET_IPC_OK ||
        !message(h, POCKET_IPC_SNAPSHOT, id, &end_message) ||
        end_message.flags != POCKET_IPC_SNAPSHOT_END || end_message.revision != begin_message.revision) return 0;
    if (revision) *revision = end_message.revision;
    return 1;
}

static int one_snapshot(Harness *h, uint64_t id, uint64_t key, const char *payload, uint64_t *revision) {
    PocketIpcMessage begin_message, item, end_message;
    if (!message(h, POCKET_IPC_SNAPSHOT, id, &begin_message) ||
        begin_message.flags != POCKET_IPC_SNAPSHOT_BEGIN || begin_message.status != POCKET_IPC_OK ||
        !message(h, POCKET_IPC_SNAPSHOT, id, &item) || item.flags || item.cursor != key ||
        strcmp(item.payload, payload) || item.revision != begin_message.revision ||
        !message(h, POCKET_IPC_SNAPSHOT, id, &end_message) ||
        end_message.flags != POCKET_IPC_SNAPSHOT_END || end_message.revision != begin_message.revision) return 0;
    if (revision) *revision = end_message.revision;
    return 1;
}

static void finish(Harness *h) {
    pocket_ipc_client_close(&h->client);
    for (unsigned i = 0; i < 3; ++i) pocket_service_close(&h->services[i]);
    pocket_ipc_broker_close(&h->broker);
    if (h->directory[0]) (void)rmdir(h->directory);
}

static int basic_and_membership(void) {
    Harness h;
    int ok = 0;
    CHECK(begin(&h));
    CHECK(open_service(&h, 0, "test.counter.v1", 0));
    PocketIpcFilter active = {{0}, {0}}, idle = {{0}, {0}};
    strcpy(active.key, "status"); strcpy(active.value, "active");
    strcpy(idle.key, "status"); strcpy(idle.value, "idle");
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, &active, "one", 3) == POCKET_IPC_OK);
    CHECK(pocket_service_state_put(&h.services[0], "objects", 2, &idle, "two", 3) == POCKET_IPC_OK);
    CHECK(wait_ready(&h, 0));
    uint64_t id = 0;
    PocketIpcMessage result;
    static const char opaque[] = "opaque 中文; not JSON";
    CHECK(pocket_ipc_client_request(&h.client, POCKET_IPC_QUERY, "test.counter.v1", "get", opaque,
                                    sizeof(opaque) - 1U, WAIT_MS, pocket_ipc_monotonic_ms(), &id) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_REPLY, id, &result) && result.status == POCKET_IPC_OK &&
          result.payload_length == sizeof(opaque) - 1U && !strcmp(result.payload, opaque));
    CHECK(!h.handlers[0].key[0]);
    CHECK(pocket_ipc_client_request_keyed(&h.client, POCKET_IPC_COMMAND, "test.counter.v1", "add", "business-key-7",
                                          "reject", 6, WAIT_MS, pocket_ipc_monotonic_ms(), &id) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_REPLY, id, &result) && result.status == POCKET_IPC_REJECTED &&
          !strcmp(result.payload, "business-rejection") && !strcmp(h.handlers[0].key, "business-key-7"));
    uint64_t event = 0;
    CHECK(pocket_ipc_client_subscribe(&h.client, POCKET_IPC_SUBSCRIBE, "test.counter.v1", "changed", &active,
                                      WAIT_MS, pocket_ipc_monotonic_ms(), &event) == POCKET_IPC_OK);
    advance(&h, 10);
    CHECK(pocket_ipc_client_next(&h.client, &result) == POCKET_IPC_OK &&
          result.kind == POCKET_IPC_SUBSCRIBED && result.request_id == event);
    CHECK(pocket_service_publish(&h.services[0], "changed", &idle, "filtered", 8) == POCKET_IPC_OK);
    advance(&h, 10);
    CHECK(pocket_ipc_client_next(&h.client, &result) == POCKET_IPC_AGAIN);
    CHECK(pocket_service_publish(&h.services[0], "changed", &active, "visible", 7) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_EVENT, event, &result) && result.cursor == 2 && !strcmp(result.payload, "visible"));
    CHECK(pocket_ipc_client_unsubscribe(&h.client, event) == POCKET_IPC_OK);
    advance(&h, 10);
    uint64_t watch = 0, revision = 0;
    CHECK(subscribe(&h, "test.counter.v1", "objects", &active, &watch));
    CHECK(one_snapshot(&h, watch, 1, "one", &revision) && revision == 2);
    CHECK(pocket_service_state_put(&h.services[0], "objects", 2, &active, "entered", 7) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_STATE_DELTA, watch, &result) && result.cursor == 2 && result.revision == 3 && !strcmp(result.payload, "entered"));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, &active, "updated", 7) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_STATE_DELTA, watch, &result) && result.cursor == 1 && result.revision == 4 && !strcmp(result.payload, "updated"));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, &idle, "left", 4) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_STATE_REMOVE, watch, &result) && result.cursor == 1 && result.revision == 5 &&
          !strcmp(result.payload, "{\"reason\":\"filter_exit\"}"));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, &idle, "hidden", 6) == POCKET_IPC_OK);
    advance(&h, 10);
    CHECK(pocket_ipc_client_next(&h.client, &result) == POCKET_IPC_AGAIN);
    CHECK(pocket_service_state_remove(&h.services[0], "objects", 2) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_STATE_REMOVE, watch, &result) && result.cursor == 2 && result.revision == 7 &&
          !strcmp(result.payload, "{\"reason\":\"deleted\"}"));
    CHECK(pocket_ipc_client_unsubscribe(&h.client, watch) == POCKET_IPC_OK);
    advance(&h, 10);
    uint64_t replacement = 0;
    CHECK(subscribe(&h, "test.counter.v1", "objects", &idle, &replacement) && replacement != watch);
    CHECK(one_snapshot(&h, replacement, 1, "hidden", &revision) && revision == 7);
    CHECK(pocket_ipc_client_unsubscribe(&h.client, replacement) == POCKET_IPC_OK);
    advance(&h, 10);
    CHECK(subscribe(&h, "test.counter.v1", "objects", &active, &replacement));
    CHECK(empty_snapshot(&h, replacement, &revision) && revision == 7);
    ok = 1;
fail:
    finish(&h);
    return ok;
}

static int capacity_and_reply_reservation(void) {
    Harness h;
    int ok = 0;
    CHECK(begin(&h));
    CHECK(open_service(&h, 0, "capacity.v1", 0));
    CHECK(wait_ready(&h, 0));
    for (uint64_t key = 1; key <= POCKET_SERVICE_STATE_LIMIT; ++key)
        CHECK(pocket_service_state_put(&h.services[0], "objects", key, NULL, "initial", 7) == POCKET_IPC_OK);
    PocketServiceSnapshot before, after;
    CHECK(pocket_service_snapshot(&h.services[0], &before));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 999, NULL, "extra", 5) == POCKET_IPC_BUSY);
    CHECK(pocket_service_snapshot(&h.services[0], &after) && after.revision == before.revision && after.state_count == POCKET_SERVICE_STATE_LIMIT);
    uint64_t watch = 0;
    CHECK(subscribe(&h, "capacity.v1", "objects", NULL, &watch));
    PocketIpcMessage result;
    CHECK(message(&h, POCKET_IPC_SNAPSHOT, watch, &result) && result.flags == POCKET_IPC_SNAPSHOT_BEGIN);
    uint64_t revision = result.revision;
    for (uint64_t key = 1; key <= POCKET_SERVICE_STATE_LIMIT; ++key)
        CHECK(message(&h, POCKET_IPC_SNAPSHOT, watch, &result) && !result.flags && result.cursor == key && result.revision == revision);
    CHECK(message(&h, POCKET_IPC_SNAPSHOT, watch, &result) && result.flags == POCKET_IPC_SNAPSHOT_END && result.revision == revision);
    unsigned accepted = 0;
    char value[32];
    for (unsigned i = 0; i <= POCKET_IPC_TX_CAPACITY; ++i) {
        int n = snprintf(value, sizeof(value), "value-%u", i);
        CHECK(n > 0 && (size_t)n < sizeof(value));
        PocketIpcStatus status = pocket_service_state_put(&h.services[0], "objects", 1, NULL, value, (size_t)n);
        if (status == POCKET_IPC_BUSY) break;
        CHECK(status == POCKET_IPC_OK);
        ++accepted;
    }
    CHECK(accepted == POCKET_IPC_TX_CAPACITY);
    CHECK(pocket_service_snapshot(&h.services[0], &before));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, NULL, "must-not-commit", 15) == POCKET_IPC_BUSY);
    CHECK(pocket_service_snapshot(&h.services[0], &after) && after.revision == before.revision);
    for (unsigned i = 0; i < accepted; ++i)
        CHECK(message(&h, POCKET_IPC_STATE_DELTA, watch, &result) && result.revision == revision + i + 1U);
    CHECK(!strcmp(result.payload, "value-31"));
    CHECK(pocket_ipc_client_unsubscribe(&h.client, watch) == POCKET_IPC_OK);
    advance(&h, 10);
    uint64_t request = 0;
    CHECK(pocket_ipc_client_request(&h.client, POCKET_IPC_QUERY, "capacity.v1", "get", "burst", 5,
                                    WAIT_MS, pocket_ipc_monotonic_ms(), &request) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_REPLY, request, &result) && result.status == POCKET_IPC_OK && !strcmp(result.payload, "burst"));
    CHECK(h.handlers[0].burst == POCKET_IPC_TX_CAPACITY - 1U && h.handlers[0].burst_status == POCKET_IPC_BUSY);
    /* A handler cannot recursively pump its owner or free it while executing. */
    CHECK(pocket_ipc_client_request(&h.client, POCKET_IPC_QUERY, "capacity.v1", "get", "close", 5,
                                    WAIT_MS, pocket_ipc_monotonic_ms(), &request) == POCKET_IPC_OK);
    CHECK(message(&h, 0, request, &result) && result.status != POCKET_IPC_OK && !h.services[0].impl);
    ok = 1;
fail:
    finish(&h);
    return ok;
}

static int dynamic_registration_and_recovery(void) {
    Harness h;
    int ok = 0;
    CHECK(begin(&h));
    CHECK(open_service(&h, 0, "original.v1", 0));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, NULL, "before", 6) == POCKET_IPC_OK);
    CHECK(wait_ready(&h, 0));
    PocketServiceSnapshot original, replacement, duplicate;
    PocketIpcClientStats client_before, client_after;
    CHECK(pocket_service_snapshot(&h.services[0], &original));
    CHECK(pocket_ipc_client_stats(&h.client, &client_before) == POCKET_IPC_OK);
    uint64_t watch = 0;
    CHECK(subscribe(&h, "original.v1", "objects", NULL, &watch));
    CHECK(one_snapshot(&h, watch, 1, "before", NULL));
    /* Add an independently named service and members without touching broker. */
    CHECK(open_service(&h, 1, "vendor.unplanned.v1", 1));
    CHECK(wait_ready(&h, 1));
    uint64_t request = 0;
    PocketIpcMessage result;
    CHECK(pocket_ipc_client_request(&h.client, POCKET_IPC_QUERY, "vendor.unplanned.v1", "inspect", "new-schema", 10,
                                    WAIT_MS, pocket_ipc_monotonic_ms(), &request) == POCKET_IPC_OK);
    CHECK(message(&h, POCKET_IPC_REPLY, request, &result) && result.status == POCKET_IPC_OK && !strcmp(result.payload, "new-schema"));
    CHECK(pocket_ipc_client_stats(&h.client, &client_after) == POCKET_IPC_OK &&
          client_after.broker_epoch == client_before.broker_epoch && client_after.connection_epoch == client_before.connection_epoch);
    /* A second peer cannot take ownership of an already registered service. */
    CHECK(open_service(&h, 2, "original.v1", 0));
    uint64_t until = pocket_ipc_monotonic_ms() + WAIT_MS;
    do {
        advance(&h, 1);
        CHECK(pocket_service_snapshot(&h.services[2], &duplicate));
    } while (duplicate.connected && pocket_ipc_monotonic_ms() < until);
    CHECK(!duplicate.connected && !duplicate.ready);
    CHECK(pocket_service_snapshot(&h.services[0], &replacement) && replacement.ready && replacement.epoch == original.epoch);
    pocket_service_close(&h.services[0]);
    CHECK(message(&h, 0, watch, &result) &&
          (result.kind == POCKET_IPC_STATE_RESET || result.kind == POCKET_IPC_SERVICE_DOWN) && result.status != POCKET_IPC_OK);
    CHECK(open_service(&h, 0, "original.v1", 0));
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, NULL, "after", 5) == POCKET_IPC_OK);
    CHECK(wait_ready(&h, 0));
    CHECK(pocket_service_snapshot(&h.services[0], &replacement) && replacement.epoch != original.epoch);
    uint64_t fresh_watch = 0;
    CHECK(subscribe(&h, "original.v1", "objects", NULL, &fresh_watch) && fresh_watch != watch);
    CHECK(one_snapshot(&h, fresh_watch, 1, "after", NULL));
    /* Router recovery requires explicit new sessions and a fresh snapshot. */
    pocket_ipc_broker_close(&h.broker);
    advance(&h, 10);
    CHECK(message(&h, 0, fresh_watch, &result) && result.status != POCKET_IPC_OK);
    CHECK(open_broker(&h));
    CHECK(pocket_service_reconnect(&h.services[0]) == POCKET_IPC_OK);
    CHECK(pocket_service_reconnect(&h.services[1]) == POCKET_IPC_OK);
    CHECK(pocket_ipc_client_reconnect(&h.client) == POCKET_IPC_OK);
    CHECK(wait_ready(&h, 0) && wait_ready(&h, 1));
    CHECK(pocket_ipc_client_stats(&h.client, &client_after) == POCKET_IPC_OK && client_after.broker_epoch != client_before.broker_epoch);
    CHECK(subscribe(&h, "original.v1", "objects", NULL, &fresh_watch));
    CHECK(one_snapshot(&h, fresh_watch, 1, "after", NULL));
    pocket_ipc_client_close(&h.client);
    advance(&h, 10);
    CHECK(pocket_service_snapshot(&h.services[0], &replacement) && replacement.ready && !replacement.subscription_count && replacement.state_count == 1);
    CHECK(open_client(&h) && wait_ready(&h, 0));
    CHECK(subscribe(&h, "original.v1", "objects", NULL, &fresh_watch));
    CHECK(one_snapshot(&h, fresh_watch, 1, "after", NULL));
    ok = 1;
fail:
    finish(&h);
    return ok;
}

static int validation_and_budgets(void) {
    Harness h;
    int ok = 0;
    CHECK(begin(&h));
    PocketService empty = {0};
    PocketServiceConfig invalid = pocket_service_config();
    CHECK(pocket_service_open(&empty, &invalid) == POCKET_IPC_INVALID && !empty.impl);
    CHECK(open_service(&h, 0, "validation.v1", 0));
    CHECK(wait_ready(&h, 0));
    CHECK(pocket_service_pump(&h.services[0], 0) == POCKET_IPC_INVALID);
    CHECK(pocket_service_pump(&h.services[0], 65) == POCKET_IPC_INVALID);
    CHECK(pocket_service_state_put(&h.services[0], "objects", 0, NULL, "x", 1) == POCKET_IPC_INVALID);
    CHECK(pocket_service_state_put(&h.services[0], "unknown", 1, NULL, "x", 1) == POCKET_IPC_NOT_FOUND);
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, NULL, "x\0y", 3) == POCKET_IPC_INVALID);
    CHECK(pocket_service_state_put(&h.services[0], "objects", 1, NULL, "\xc0\xaf", 2) == POCKET_IPC_INVALID);
    CHECK(pocket_service_state_remove(&h.services[0], "objects", 99) == POCKET_IPC_NOT_FOUND);
    PocketServiceReply reply;
    CHECK(!pocket_service_error(&reply, POCKET_IPC_AGAIN, "null", 4));
    CHECK(pocket_service_reply(&reply, "中文", 6) && reply.payload_length == 6);
    uint64_t requests[4];
    for (unsigned i = 0; i < 4; ++i)
        CHECK(pocket_ipc_client_request(&h.client, POCKET_IPC_QUERY, "validation.v1", "get", "budget", 6,
                                        WAIT_MS, pocket_ipc_monotonic_ms(), &requests[i]) == POCKET_IPC_OK);
    CHECK(pocket_ipc_client_step(&h.client, pocket_ipc_monotonic_ms(), 32) == POCKET_IPC_OK);
    for (unsigned i = 0; i < 4; ++i) CHECK(pocket_ipc_broker_step(&h.broker, pocket_ipc_monotonic_ms(), 32) == POCKET_IPC_OK);
    unsigned calls = h.handlers[0].calls;
    CHECK(pocket_service_pump(&h.services[0], 1) == POCKET_IPC_OK && h.handlers[0].calls == calls + 1U);
    PocketIpcMessage result;
    for (unsigned i = 0; i < 4; ++i)
        CHECK(message(&h, POCKET_IPC_REPLY, requests[i], &result) && result.status == POCKET_IPC_OK);
    CHECK(h.handlers[0].calls == calls + 4U);
    CHECK(h.handlers[0].reentry_status == POCKET_IPC_INVALID);
    ok = 1;
fail:
    finish(&h);
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--intentional-failure")) return 1;
    if (argc > 2) return 2;
    int (*cases[])(void) = {basic_and_membership, capacity_and_reply_reservation,
                           dynamic_registration_and_recovery, validation_and_budgets};
    const char *names[] = {"opaque-request-business-key-filter-membership", "atomic-snapshot-backpressure-reply-reservation",
                           "dynamic-registration-conflict-restart-recovery", "validation-owner-pump-budget"};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!cases[i]()) return 1;
        printf("IPC_SERVICE_CASE_PASS %s\n", names[i]);
    }
    puts("IPC_SERVICE_OK real-af-unix-provider; no-device-no-physical-performance-admission");
    return 0;
}
