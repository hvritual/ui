#ifndef POCKET_IPC_WIRE_H
#define POCKET_IPC_WIRE_H

#include <stddef.h>
#include <stdint.h>

/* Local, versioned IPC. All objects are single-owner; no JS/native handles cross
 * the wire. Strings are bounded UTF-8 without embedded NUL; names are ASCII. */
#define POCKET_IPC_VERSION 1u
#define POCKET_IPC_NAME_MAX 63u
#define POCKET_IPC_FILTER_KEY_MAX 31u
#define POCKET_IPC_FILTER_VALUE_MAX 95u
#define POCKET_IPC_PAYLOAD_MAX 768u
#define POCKET_IPC_HEADER_BYTES 82u
#define POCKET_IPC_FRAME_MAX (4u + POCKET_IPC_HEADER_BYTES + 3u * POCKET_IPC_NAME_MAX + POCKET_IPC_FILTER_KEY_MAX + POCKET_IPC_FILTER_VALUE_MAX + POCKET_IPC_PAYLOAD_MAX)
#define POCKET_IPC_TX_CAPACITY 32u
#define POCKET_IPC_SOCKET_PATH_MAX 107u
#define POCKET_IPC_TIMEOUT_MAX_MS 120000u

typedef enum {
    POCKET_IPC_OK = 0, POCKET_IPC_AGAIN = 1,
    POCKET_IPC_INVALID = 2, POCKET_IPC_UNAVAILABLE = 3,
    POCKET_IPC_UNAUTHORIZED = 4, POCKET_IPC_TIMEOUT = 5,
    POCKET_IPC_CANCELLED = 6, POCKET_IPC_DISCONNECTED = 7,
    POCKET_IPC_BUSY = 8, POCKET_IPC_TOO_LARGE = 9,
    POCKET_IPC_PROTOCOL = 10, POCKET_IPC_STALE = 11,
    POCKET_IPC_NOT_FOUND = 12, POCKET_IPC_INTERNAL = 13,
    POCKET_IPC_REJECTED = 14
} PocketIpcStatus;

typedef enum {
    POCKET_IPC_WELCOME = 1,
    POCKET_IPC_REGISTER = 2, POCKET_IPC_REGISTERED = 3,
    POCKET_IPC_QUERY = 4, POCKET_IPC_COMMAND = 5, POCKET_IPC_REPLY = 6,
    POCKET_IPC_SUBSCRIBE = 7, POCKET_IPC_UNSUBSCRIBE = 8,
    POCKET_IPC_EVENT = 9, POCKET_IPC_WATCH = 10, POCKET_IPC_UNWATCH = 11,
    POCKET_IPC_SNAPSHOT = 12, POCKET_IPC_STATE_DELTA = 13,
    POCKET_IPC_STATE_REMOVE = 14, POCKET_IPC_STATE_RESET = 15,
    POCKET_IPC_CANCEL = 16, POCKET_IPC_SUBSCRIBED = 17,
    POCKET_IPC_ERROR = 18, POCKET_IPC_SERVICE_DOWN = 19
} PocketIpcKind;

enum {
    POCKET_IPC_ALLOW_QUERY = 1u, POCKET_IPC_ALLOW_COMMAND = 2u,
    POCKET_IPC_ALLOW_EVENT = 4u, POCKET_IPC_ALLOW_STATE = 8u,
    POCKET_IPC_ALLOW_ALL = 15u,
    POCKET_IPC_SNAPSHOT_BEGIN = 1u << 8,
    POCKET_IPC_SNAPSHOT_END = 1u << 9,
    POCKET_IPC_OUTCOME_UNKNOWN = 1u << 10
};

/* One exact equality selector. Empty key/value means all items/events.
 * Event publishers attach matching metadata; broker performs equality.
 * State selectors are forwarded unchanged; provider owns membership/removal. */
typedef struct {
    char key[POCKET_IPC_FILTER_KEY_MAX + 1u];
    char value[POCKET_IPC_FILTER_VALUE_MAX + 1u];
} PocketIpcFilter;

typedef struct {
    PocketIpcKind kind;
    uint32_t flags;
    PocketIpcStatus status;
    uint64_t broker_epoch;
    uint64_t connection_epoch; /* requester route; assigned by broker WELCOME */
    uint64_t request_id;       /* request OR subscription; unique per connection */
    uint64_t service_epoch;    /* broker-owned service incarnation */
    uint64_t revision;         /* provider-owned state revision */
    uint64_t cursor;           /* provider-owned event cursor / item key */
    uint32_t timeout_ms;
    char service[POCKET_IPC_NAME_MAX + 1u];
    char member[POCKET_IPC_NAME_MAX + 1u];
    char idempotency_key[POCKET_IPC_NAME_MAX + 1u]; /* Command key; provider owns durable deduplication */
    PocketIpcFilter filter;
    uint32_t payload_length;
    char payload[POCKET_IPC_PAYLOAD_MAX + 1u]; /* opaque UTF-8, not trusted JSON */
} PocketIpcMessage;

int pocket_ipc_name_valid(const char *name, size_t maximum);
int pocket_ipc_utf8_valid(const char *text, size_t length);
int pocket_ipc_filter_valid(const PocketIpcFilter *filter);
int pocket_ipc_filter_matches(const PocketIpcFilter *selector, const PocketIpcFilter *metadata);
PocketIpcStatus pocket_ipc_encode(const PocketIpcMessage *message, void *bytes, size_t capacity, size_t *written);
PocketIpcStatus pocket_ipc_decode(const void *bytes, size_t length, PocketIpcMessage *message);
const char *pocket_ipc_status_name(PocketIpcStatus status);
uint64_t pocket_ipc_monotonic_ms(void);

typedef struct { void *impl; } PocketIpcChannel;
/* open/adopt never wait. adopt takes fd ownership, including on failure. */
PocketIpcStatus pocket_ipc_channel_open(PocketIpcChannel *channel, const char *socket_path);
PocketIpcStatus pocket_ipc_channel_adopt(PocketIpcChannel *channel, int fd);
PocketIpcStatus pocket_ipc_channel_step(PocketIpcChannel *channel, size_t byte_budget);
PocketIpcStatus pocket_ipc_channel_send(PocketIpcChannel *channel, const PocketIpcMessage *message);
/* Atomic bounded enqueue: all messages or none; suitable for snapshot batches. */
PocketIpcStatus pocket_ipc_channel_send_batch(PocketIpcChannel *channel, const PocketIpcMessage *messages, size_t count);
PocketIpcStatus pocket_ipc_channel_next(PocketIpcChannel *channel, PocketIpcMessage *message);
size_t pocket_ipc_channel_available(const PocketIpcChannel *channel);
int pocket_ipc_channel_fd(const PocketIpcChannel *channel);
int pocket_ipc_channel_connected(const PocketIpcChannel *channel);
void pocket_ipc_channel_close(PocketIpcChannel *channel);

#endif
