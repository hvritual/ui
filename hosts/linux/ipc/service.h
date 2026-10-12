#ifndef POCKET_IPC_SERVICE_H
#define POCKET_IPC_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include "wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Provider SDK ABI. All calls belong to one owner thread. The owner must keep
 * handlers short: the pump bounds handler count, but cannot preempt C code.
 * Long operations return an application receipt and continue outside a handler.
 * No function starts a thread, forks a worker, or accesses a device. */
#define POCKET_SERVICE_ABI 1U
#define POCKET_SERVICE_MEMBER_LIMIT 16U
#define POCKET_SERVICE_STATE_LIMIT 16U
#define POCKET_SERVICE_WATCH_LIMIT 16U
#define POCKET_SERVICE_PAYLOAD_LIMIT POCKET_IPC_PAYLOAD_MAX

typedef enum {
    POCKET_SERVICE_QUERY = 1,
    POCKET_SERVICE_COMMAND = 2,
    POCKET_SERVICE_EVENT = 3,
    POCKET_SERVICE_STATE = 4
} PocketServiceKind;

/* Request data is borrowed only until its handler returns. Copy data required
 * by a later operation. Application payloads are opaque UTF-8 to this SDK;
 * each handler is responsible for its own JSON/schema and business validation.
 * An idempotency key belongs to the business request and its durable authority;
 * this SDK never substitutes a transport request ID for an operation key. */
typedef struct {
    PocketServiceKind kind;
    const char *member;
    const char *payload;
    size_t payload_length;
    const char *idempotency_key;
    uint64_t request_id;
} PocketServiceRequest;

typedef struct {
    PocketIpcStatus status;
    uint32_t flags;
    char payload[POCKET_SERVICE_PAYLOAD_LIMIT + 1U];
    size_t payload_length;
} PocketServiceReply;

typedef void (*PocketServiceHandler)(void *context,
                                     const PocketServiceRequest *request,
                                     PocketServiceReply *reply);

typedef struct {
    PocketServiceKind kind;
    const char *name;
    PocketServiceHandler handler; /* required only for Query / Command */
    void *context;
} PocketServiceMember;

typedef struct {
    uint32_t abi, size;
    const char *socket_path;
    const char *service;
    const PocketServiceMember *members;
    size_t member_count;
} PocketServiceConfig;

typedef struct { void *impl; } PocketService;

typedef struct {
    int connected, ready;
    uint64_t broker_epoch, connection_epoch;
    uint64_t epoch, revision;
    size_t state_count, subscription_count;
    uint64_t requests, events, state_updates, rejected;
} PocketServiceSnapshot;

PocketServiceConfig pocket_service_config(void);
/* open copies names, member records, socket path and service ID. Handler
 * contexts remain caller-owned until close. Registration completes in pump. */
PocketIpcStatus pocket_service_open(PocketService *, const PocketServiceConfig *);
/* Reconnect keeps the provider's state store and handler configuration, clears
 * old subscriptions and uses a fresh transport/session epoch. The caller owns
 * retry scheduling. A disconnected service never executes a queued command. */
PocketIpcStatus pocket_service_reconnect(PocketService *);
/* At most frame_budget input messages and handler calls; no blocking waits.
 * Recursive pump is rejected. Each handler may publish or mutate state. */
PocketIpcStatus pocket_service_pump(PocketService *, unsigned frame_budget);
int pocket_service_snapshot(const PocketService *, PocketServiceSnapshot *);
void pocket_service_close(PocketService *);

/* Reply helpers copy data. Expected business failures are ordinary replies.
 * Neither helper validates a business schema or implies physical completion. */
int pocket_service_reply(PocketServiceReply *, const char *payload, size_t length);
int pocket_service_error(PocketServiceReply *, PocketIpcStatus status,
                         const char *payload, size_t length);
/* Use after a Command may have changed its authority but cannot establish a
 * definitive result. Ordinary error() asserts a known business rejection;
 * missing or malformed handler replies conservatively become uncertain. */
int pocket_service_error_uncertain(PocketServiceReply *, PocketIpcStatus status,
                                   const char *payload, size_t length);

PocketIpcStatus pocket_service_publish(PocketService *, const char *event,
                                       const PocketIpcFilter *metadata,
                                       const char *payload, size_t payload_length);
/* The bounded store is owned by the provider, never by the router or UI.
 * Watch installation, snapshot and mutations are serialized by this owner.
 * A rejected mutation leaves the store unchanged; an external authoritative
 * source must retry its latest projection or force a new snapshot on failure. */
/* State keys are nonzero uint64 values stable within a service incarnation.
 * String business keys may additionally appear in opaque payloads. The v1
 * selector supports one metadata key/value equality, or an empty selector. */
PocketIpcStatus pocket_service_state_put(PocketService *, const char *collection,
                                         uint64_t key, const PocketIpcFilter *metadata,
                                         const char *payload, size_t payload_length);
PocketIpcStatus pocket_service_state_remove(PocketService *, const char *collection,
                                            uint64_t key);

#ifdef __cplusplus
}
#endif
#endif
