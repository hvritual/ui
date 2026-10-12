#ifndef POCKET_IPC_CLIENT_H
#define POCKET_IPC_CLIENT_H
#include "wire.h"

#define POCKET_IPC_PENDING_CAPACITY 32u
#define POCKET_IPC_SUBSCRIPTION_CAPACITY 16u
#define POCKET_IPC_INBOX_CAPACITY 64u

typedef struct { void *impl; } PocketIpcClient;
typedef struct {
    const char *socket_path;    /* trusted host configuration, copied on open */
    const char *application_id; /* bounded copied diagnostics, never authority */
    uint32_t allow;             /* trusted native capability grant, not wire data */
} PocketIpcClientConfig;
typedef struct {
    uint64_t broker_epoch, connection_epoch, late_messages;
    uint32_t pending, subscriptions, queued;
    int ready;
    PocketIpcStatus status;
} PocketIpcClientStats;

/* All APIs run on the UI owner. No blocking waits or callbacks. open initiates
 * connection; ready is obtained after step receives WELCOME. allow==0 denies. */
PocketIpcStatus pocket_ipc_client_open(PocketIpcClient *client, const PocketIpcClientConfig *config);
PocketIpcStatus pocket_ipc_client_step(PocketIpcClient *client, uint64_t now_ms, uint32_t message_budget);
/* AGAIN means not accepted yet (e.g. handshake); no request ID is consumed.
 * After OK exactly one terminal result is queued unless caller closes client. */
PocketIpcStatus pocket_ipc_client_request(PocketIpcClient *client, PocketIpcKind kind,
    const char *service, const char *member, const char *payload, size_t payload_length,
    uint32_t timeout_ms, uint64_t now_ms, uint64_t *request_id);
/* Command requires a nonempty bounded key; payload remains byte-for-byte opaque.
 * Query must use an empty key. Broker validates presence, provider owns dedup. */
PocketIpcStatus pocket_ipc_client_request_keyed(PocketIpcClient *client, PocketIpcKind kind,
    const char *service, const char *member, const char *idempotency_key,
    const char *payload, size_t payload_length, uint32_t timeout_ms,
    uint64_t now_ms, uint64_t *request_id);
PocketIpcStatus pocket_ipc_client_subscribe(PocketIpcClient *client, PocketIpcKind kind,
    const char *service, const char *member, const PocketIpcFilter *filter,
    uint32_t timeout_ms, uint64_t now_ms, uint64_t *subscription_id);
PocketIpcStatus pocket_ipc_client_cancel(PocketIpcClient *client, uint64_t request_id);
PocketIpcStatus pocket_ipc_client_unsubscribe(PocketIpcClient *client, uint64_t subscription_id);
/* next consumes queued results only; no network IO. Payload is always opaque. */
PocketIpcStatus pocket_ipc_client_next(PocketIpcClient *client, PocketIpcMessage *message);
PocketIpcStatus pocket_ipc_client_stats(const PocketIpcClient *client, PocketIpcClientStats *stats);
/* Explicit recovery invalidates old work; never silently replays commands or
 * subscriptions. Old terminal results remain available through next(). */
PocketIpcStatus pocket_ipc_client_reconnect(PocketIpcClient *client);
/* Closing invalidates first, drops callbacks/results, then frees. Idempotent. */
void pocket_ipc_client_close(PocketIpcClient *client);
#endif
