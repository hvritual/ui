#include "service.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE_PUMP_MAX 64U
#define SERVICE_BATCH_MAX (POCKET_SERVICE_STATE_LIMIT + 2U)
#define SERVICE_HANDSHAKE_MS 5000U

typedef struct {
    PocketServiceKind kind;
    char name[POCKET_IPC_NAME_MAX + 1U];
    PocketServiceHandler handler;
    void *context;
    int registered;
} Member;

typedef struct {
    int used;
    unsigned member;
    uint64_t key;
    PocketIpcFilter metadata;
    uint32_t length;
    char payload[POCKET_IPC_PAYLOAD_MAX + 1U];
} State;

typedef struct {
    int used;
    unsigned member;
    uint64_t broker_epoch, connection_epoch, request_id;
    PocketIpcFilter selector;
} Watch;

typedef struct {
    PocketIpcChannel channel;
    char socket_path[POCKET_IPC_SOCKET_PATH_MAX + 1U];
    char service[POCKET_IPC_NAME_MAX + 1U];
    Member members[POCKET_SERVICE_MEMBER_LIMIT];
    size_t member_count, registered_count;
    State states[POCKET_SERVICE_STATE_LIMIT];
    Watch watches[POCKET_SERVICE_WATCH_LIMIT];
    PocketIpcMessage batch[SERVICE_BATCH_MAX];
    uint64_t broker_epoch, connection_epoch, service_epoch, revision, event_cursor;
    uint64_t handshake_deadline;
    uint64_t requests, events, state_updates, rejected;
    int in_pump, close_requested, reply_reserved;
} Service;

static int valid_payload(const char *payload, size_t length) {
    return payload && length <= POCKET_IPC_PAYLOAD_MAX &&
           !memchr(payload, 0, length) && pocket_ipc_utf8_valid(payload, length);
}

static uint64_t handshake_deadline(void) {
    uint64_t now = pocket_ipc_monotonic_ms();
    return now > UINT64_MAX - SERVICE_HANDSHAKE_MS ? UINT64_MAX : now + SERVICE_HANDSHAKE_MS;
}

static uint32_t member_flag(PocketServiceKind kind) {
    switch (kind) {
    case POCKET_SERVICE_QUERY: return POCKET_IPC_ALLOW_QUERY;
    case POCKET_SERVICE_COMMAND: return POCKET_IPC_ALLOW_COMMAND;
    case POCKET_SERVICE_EVENT: return POCKET_IPC_ALLOW_EVENT;
    case POCKET_SERVICE_STATE: return POCKET_IPC_ALLOW_STATE;
    default: return 0;
    }
}

static int ready(const Service *s) {
    return s && !s->close_requested && s->service_epoch && s->registered_count == s->member_count &&
           pocket_ipc_channel_connected(&s->channel);
}

static int find_member(const Service *s, const char *name, PocketServiceKind kind) {
    if (!name) return -1;
    for (size_t i = 0; i < s->member_count; ++i)
        if (s->members[i].kind == kind && !strcmp(s->members[i].name, name)) return (int)i;
    return -1;
}

static void disconnected(Service *s) {
    pocket_ipc_channel_close(&s->channel);
    memset(s->watches, 0, sizeof(s->watches));
    for (size_t i = 0; i < s->member_count; ++i) s->members[i].registered = 0;
    s->registered_count = 0;
    s->broker_epoch = s->connection_epoch = s->service_epoch = 0;
}

static PocketIpcStatus enqueue(Service *s, PocketIpcMessage *messages, size_t count) {
    if (pocket_ipc_channel_fd(&s->channel) < 0) return POCKET_IPC_DISCONNECTED;
    if (count > pocket_ipc_channel_available(&s->channel) ||
        (size_t)s->reply_reserved > pocket_ipc_channel_available(&s->channel) - count)
        return POCKET_IPC_BUSY;
    return pocket_ipc_channel_send_batch(&s->channel, messages, count);
}

static void base_message(const Service *s, PocketIpcMessage *message,
                         PocketIpcKind kind, unsigned member) {
    memset(message, 0, sizeof(*message));
    message->kind = kind;
    message->broker_epoch = s->broker_epoch;
    message->connection_epoch = s->connection_epoch;
    message->service_epoch = s->service_epoch;
    memcpy(message->service, s->service, strlen(s->service) + 1U);
    memcpy(message->member, s->members[member].name, strlen(s->members[member].name) + 1U);
}

static void watch_message(const Service *s, PocketIpcMessage *message,
                          PocketIpcKind kind, const Watch *watch) {
    base_message(s, message, kind, watch->member);
    message->broker_epoch = watch->broker_epoch;
    message->connection_epoch = watch->connection_epoch;
    message->request_id = watch->request_id;
    message->filter = watch->selector;
}

static void payload_copy(PocketIpcMessage *message, const char *payload, size_t length) {
    if (length) memcpy(message->payload, payload, length);
    message->payload[length] = 0;
    message->payload_length = (uint32_t)length;
}

PocketServiceConfig pocket_service_config(void) {
    PocketServiceConfig config;
    memset(&config, 0, sizeof(config));
    config.abi = POCKET_SERVICE_ABI;
    config.size = sizeof(config);
    return config;
}

PocketIpcStatus pocket_service_open(PocketService *service, const PocketServiceConfig *config) {
    if (!service || service->impl || !config || config->abi != POCKET_SERVICE_ABI ||
        config->size != sizeof(*config) || !config->socket_path ||
        !config->socket_path[0] || strlen(config->socket_path) > POCKET_IPC_SOCKET_PATH_MAX ||
        !pocket_ipc_name_valid(config->service, POCKET_IPC_NAME_MAX) ||
        !config->members || !config->member_count || config->member_count > POCKET_SERVICE_MEMBER_LIMIT)
        return POCKET_IPC_INVALID;
    for (size_t i = 0; i < config->member_count; ++i) {
        const PocketServiceMember *member = &config->members[i];
        int request = member->kind == POCKET_SERVICE_QUERY || member->kind == POCKET_SERVICE_COMMAND;
        if (!member_flag(member->kind) || !pocket_ipc_name_valid(member->name, POCKET_IPC_NAME_MAX) ||
            (request && !member->handler) || (!request && member->handler)) return POCKET_IPC_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(member->name, config->members[j].name)) return POCKET_IPC_INVALID;
    }
    Service *s = calloc(1, sizeof(*s));
    if (!s) return POCKET_IPC_BUSY;
    memcpy(s->socket_path, config->socket_path, strlen(config->socket_path) + 1U);
    memcpy(s->service, config->service, strlen(config->service) + 1U);
    s->member_count = config->member_count;
    for (size_t i = 0; i < s->member_count; ++i) {
        s->members[i].kind = config->members[i].kind;
        memcpy(s->members[i].name, config->members[i].name, strlen(config->members[i].name) + 1U);
        s->members[i].handler = config->members[i].handler;
        s->members[i].context = config->members[i].context;
    }
    PocketIpcStatus status = pocket_ipc_channel_open(&s->channel, s->socket_path);
    if (status != POCKET_IPC_OK && status != POCKET_IPC_AGAIN) { free(s); return status; }
    service->impl = s;
    s->handshake_deadline = handshake_deadline();
    return POCKET_IPC_OK;
}

PocketIpcStatus pocket_service_reconnect(PocketService *service) {
    Service *s = service ? service->impl : NULL;
    if (!s || s->in_pump) return POCKET_IPC_INVALID;
    disconnected(s);
    PocketIpcStatus status = pocket_ipc_channel_open(&s->channel, s->socket_path);
    s->handshake_deadline = handshake_deadline();
    return status == POCKET_IPC_AGAIN ? POCKET_IPC_OK : status;
}

int pocket_service_reply(PocketServiceReply *reply, const char *payload, size_t length) {
    return pocket_service_error(reply, POCKET_IPC_OK, payload, length);
}

int pocket_service_error(PocketServiceReply *reply, PocketIpcStatus status,
                         const char *payload, size_t length) {
    if (!reply || status < POCKET_IPC_OK || status > POCKET_IPC_REJECTED ||
        status == POCKET_IPC_AGAIN || !valid_payload(payload, length)) return 0;
    reply->status = status;
    if (length) memcpy(reply->payload, payload, length);
    reply->payload[length] = 0;
    reply->payload_length = length;
    return 1;
}

static PocketIpcStatus register_members(Service *s, const PocketIpcMessage *welcome) {
    if (s->broker_epoch || !welcome->broker_epoch || !welcome->connection_epoch) return POCKET_IPC_PROTOCOL;
    s->broker_epoch = welcome->broker_epoch;
    s->connection_epoch = welcome->connection_epoch;
    for (size_t i = 0; i < s->member_count; ++i) {
        base_message(s, &s->batch[i], POCKET_IPC_REGISTER, (unsigned)i);
        s->batch[i].request_id = i + 1U;
        s->batch[i].flags = member_flag(s->members[i].kind);
    }
    return enqueue(s, s->batch, s->member_count);
}

static PocketIpcStatus registered(Service *s, const PocketIpcMessage *message) {
    if (message->broker_epoch != s->broker_epoch || message->connection_epoch != s->connection_epoch ||
        message->status != POCKET_IPC_OK || !message->service_epoch ||
        strcmp(message->service, s->service)) return POCKET_IPC_PROTOCOL;
    if (s->service_epoch && s->service_epoch != message->service_epoch) return POCKET_IPC_PROTOCOL;
    for (size_t i = 0; i < s->member_count; ++i) {
        if (!strcmp(s->members[i].name, message->member)) {
            if (s->members[i].registered) return POCKET_IPC_PROTOCOL;
            s->members[i].registered = 1;
            ++s->registered_count;
            s->service_epoch = message->service_epoch;
            return POCKET_IPC_OK;
        }
    }
    return POCKET_IPC_PROTOCOL;
}

static PocketIpcStatus dispatch_request(Service *s, const PocketIpcMessage *message) {
    PocketServiceKind kind = message->kind == POCKET_IPC_QUERY ? POCKET_SERVICE_QUERY : POCKET_SERVICE_COMMAND;
    int member = find_member(s, message->member, kind);
    PocketIpcMessage response = *message;
    response.kind = POCKET_IPC_REPLY;
    response.flags = 0;
    response.revision = s->revision;
    if (member < 0 || !s->members[member].registered) {
        response.status = POCKET_IPC_NOT_FOUND;
        payload_copy(&response, "null", 4);
        ++s->rejected;
        return enqueue(s, &response, 1);
    }
    if (!pocket_ipc_channel_available(&s->channel)) return POCKET_IPC_BUSY;
    PocketServiceRequest request = {kind, s->members[member].name, message->payload,
                                    message->payload_length, message->idempotency_key, message->request_id};
    PocketServiceReply reply;
    memset(&reply, 0, sizeof(reply));
    pocket_service_error(&reply, POCKET_IPC_INTERNAL, "null", 4);
    s->reply_reserved = 1;
    ++s->requests;
    s->members[member].handler(s->members[member].context, &request, &reply);
    s->reply_reserved = 0;
    if (s->close_requested) return POCKET_IPC_DISCONNECTED;
    if (reply.status < POCKET_IPC_OK || reply.status > POCKET_IPC_REJECTED ||
        reply.status == POCKET_IPC_AGAIN || !valid_payload(reply.payload, reply.payload_length))
        pocket_service_error(&reply, POCKET_IPC_INTERNAL, "null", 4);
    response.status = reply.status;
    response.revision = s->revision;
    payload_copy(&response, reply.payload, reply.payload_length);
    return enqueue(s, &response, 1);
}

static int watch_equal(const Watch *watch, const PocketIpcMessage *message) {
    return watch->used && watch->broker_epoch == message->broker_epoch &&
           watch->connection_epoch == message->connection_epoch && watch->request_id == message->request_id;
}

static PocketIpcStatus reject_watch(Service *s, const PocketIpcMessage *message, PocketIpcStatus status) {
    PocketIpcMessage response = *message;
    response.kind = POCKET_IPC_STATE_RESET;
    response.status = status;
    response.flags = 0;
    response.revision = s->revision;
    payload_copy(&response, "null", 4);
    ++s->rejected;
    return enqueue(s, &response, 1);
}

static PocketIpcStatus install_watch(Service *s, const PocketIpcMessage *message) {
    int member = find_member(s, message->member, POCKET_SERVICE_STATE);
    if (member < 0 || !s->members[member].registered)
        return reject_watch(s, message, POCKET_IPC_NOT_FOUND);
    Watch watch;
    memset(&watch, 0, sizeof(watch));
    watch.used = 1;
    watch.member = (unsigned)member;
    watch.broker_epoch = message->broker_epoch;
    watch.connection_epoch = message->connection_epoch;
    watch.request_id = message->request_id;
    watch.selector = message->filter;
    int slot = -1;
    for (unsigned i = 0; i < POCKET_SERVICE_WATCH_LIMIT; ++i) {
        if (watch_equal(&s->watches[i], message)) return POCKET_IPC_PROTOCOL;
        if (!s->watches[i].used && slot < 0) slot = (int)i;
    }
    if (slot < 0) return reject_watch(s, message, POCKET_IPC_BUSY);
    size_t count = 0;
    watch_message(s, &s->batch[count], POCKET_IPC_SNAPSHOT, &watch);
    s->batch[count].flags = POCKET_IPC_SNAPSHOT_BEGIN;
    s->batch[count++].revision = s->revision;
    for (unsigned i = 0; i < POCKET_SERVICE_STATE_LIMIT; ++i) {
        State *state = &s->states[i];
        if (!state->used || state->member != (unsigned)member ||
            !pocket_ipc_filter_matches(&watch.selector, &state->metadata)) continue;
        PocketIpcMessage *item = &s->batch[count++];
        watch_message(s, item, POCKET_IPC_SNAPSHOT, &watch);
        item->cursor = state->key;
        item->revision = s->revision;
        item->filter = state->metadata;
        payload_copy(item, state->payload, state->length);
    }
    watch_message(s, &s->batch[count], POCKET_IPC_SNAPSHOT, &watch);
    s->batch[count].flags = POCKET_IPC_SNAPSHOT_END;
    s->batch[count++].revision = s->revision;
    PocketIpcStatus status = enqueue(s, s->batch, count);
    if (status == POCKET_IPC_BUSY) return reject_watch(s, message, status);
    if (status != POCKET_IPC_OK) return status;
    s->watches[slot] = watch;
    return POCKET_IPC_OK;
}

static PocketIpcStatus dispatch(Service *s, const PocketIpcMessage *message) {
    if (message->kind == POCKET_IPC_WELCOME) return register_members(s, message);
    if (message->kind == POCKET_IPC_REGISTERED) return registered(s, message);
    if (message->kind == POCKET_IPC_ERROR) return message->status == POCKET_IPC_OK ? POCKET_IPC_PROTOCOL : message->status;
    if (!s->service_epoch || message->broker_epoch != s->broker_epoch ||
        message->service_epoch != s->service_epoch || !message->connection_epoch || !message->request_id ||
        strcmp(message->service, s->service)) return POCKET_IPC_PROTOCOL;
    switch (message->kind) {
    case POCKET_IPC_QUERY: case POCKET_IPC_COMMAND: return dispatch_request(s, message);
    case POCKET_IPC_WATCH: return install_watch(s, message);
    case POCKET_IPC_UNWATCH:
        for (unsigned i = 0; i < POCKET_SERVICE_WATCH_LIMIT; ++i)
            if (watch_equal(&s->watches[i], message)) memset(&s->watches[i], 0, sizeof(s->watches[i]));
        return POCKET_IPC_OK;
    case POCKET_IPC_CANCEL:
        /* Cancellation ends the transport wait; it cannot roll back a handler. */
        return POCKET_IPC_OK;
    default: return POCKET_IPC_PROTOCOL;
    }
}

PocketIpcStatus pocket_service_pump(PocketService *service, unsigned frame_budget) {
    Service *s = service ? service->impl : NULL;
    if (!s || s->in_pump || !frame_budget || frame_budget > SERVICE_PUMP_MAX) return POCKET_IPC_INVALID;
    if (pocket_ipc_channel_fd(&s->channel) < 0) return POCKET_IPC_DISCONNECTED;
    if (!ready(s) && pocket_ipc_monotonic_ms() >= s->handshake_deadline) {
        disconnected(s);
        return POCKET_IPC_TIMEOUT;
    }
    s->in_pump = 1;
    PocketIpcStatus status = POCKET_IPC_OK;
    for (unsigned i = 0; i < frame_budget; ++i) {
        status = pocket_ipc_channel_step(&s->channel, POCKET_IPC_FRAME_MAX * 2U);
        if (status != POCKET_IPC_OK && status != POCKET_IPC_AGAIN) break;
        PocketIpcMessage message;
        status = pocket_ipc_channel_next(&s->channel, &message);
        if (status == POCKET_IPC_AGAIN) { status = POCKET_IPC_OK; break; }
        if (status != POCKET_IPC_OK) break;
        status = dispatch(s, &message);
        if (status != POCKET_IPC_OK || s->close_requested) break;
    }
    if (status == POCKET_IPC_OK && !s->close_requested) {
        status = pocket_ipc_channel_step(&s->channel, POCKET_IPC_FRAME_MAX * 2U);
        if (status == POCKET_IPC_AGAIN) status = POCKET_IPC_OK;
    }
    s->in_pump = 0;
    if (s->close_requested) { pocket_service_close(service); return POCKET_IPC_DISCONNECTED; }
    if (status != POCKET_IPC_OK) disconnected(s);
    return status;
}

PocketIpcStatus pocket_service_publish(PocketService *service, const char *event,
                                       const PocketIpcFilter *metadata,
                                       const char *payload, size_t payload_length) {
    Service *s = service ? service->impl : NULL;
    PocketIpcFilter empty = {{0}, {0}};
    if (!s || !valid_payload(payload, payload_length) ||
        !pocket_ipc_filter_valid(metadata ? metadata : &empty)) return POCKET_IPC_INVALID;
    if (s->close_requested) return POCKET_IPC_DISCONNECTED;
    int member = find_member(s, event, POCKET_SERVICE_EVENT);
    if (member < 0) return POCKET_IPC_NOT_FOUND;
    if (!ready(s)) return POCKET_IPC_UNAVAILABLE;
    if (s->event_cursor == UINT64_MAX) return POCKET_IPC_BUSY;
    PocketIpcMessage message;
    base_message(s, &message, POCKET_IPC_EVENT, (unsigned)member);
    message.filter = metadata ? *metadata : empty;
    message.cursor = s->event_cursor + 1U;
    payload_copy(&message, payload, payload_length);
    PocketIpcStatus status = enqueue(s, &message, 1);
    if (status == POCKET_IPC_OK) { ++s->event_cursor; ++s->events; }
    return status;
}

static PocketIpcStatus mutate_state(Service *s, unsigned member, uint64_t key,
                                    const PocketIpcFilter *metadata,
                                    const char *payload, size_t payload_length, int remove) {
    int slot = -1, empty = -1;
    for (unsigned i = 0; i < POCKET_SERVICE_STATE_LIMIT; ++i) {
        if (s->states[i].used && s->states[i].member == member && s->states[i].key == key) slot = (int)i;
        if (!s->states[i].used && empty < 0) empty = (int)i;
    }
    if (remove && slot < 0) return POCKET_IPC_NOT_FOUND;
    if (slot < 0 && empty < 0) return POCKET_IPC_BUSY;
    if (s->revision == UINT64_MAX) return POCKET_IPC_BUSY;
    State *old = slot >= 0 ? &s->states[slot] : NULL;
    if (old && !remove && old->length == payload_length &&
        !strcmp(old->metadata.key, metadata->key) && !strcmp(old->metadata.value, metadata->value) &&
        !memcmp(old->payload, payload, payload_length)) return POCKET_IPC_OK;
    size_t count = 0;
    uint64_t revision = s->revision + 1U;
    for (unsigned i = 0; i < POCKET_SERVICE_WATCH_LIMIT; ++i) {
        Watch *watch = &s->watches[i];
        if (!watch->used || watch->member != member) continue;
        int before = old && pocket_ipc_filter_matches(&watch->selector, &old->metadata);
        int after = !remove && pocket_ipc_filter_matches(&watch->selector, metadata);
        if (!before && !after) continue;
        PocketIpcMessage *message = &s->batch[count++];
        watch_message(s, message, after ? POCKET_IPC_STATE_DELTA : POCKET_IPC_STATE_REMOVE, watch);
        message->revision = revision;
        message->cursor = key;
        message->filter = after ? *metadata : old->metadata;
        if (after) payload_copy(message, payload, payload_length);
        else if (remove) payload_copy(message, "{\"reason\":\"deleted\"}", 20);
        else payload_copy(message, "{\"reason\":\"filter_exit\"}", 24);
    }
    if (count) {
        PocketIpcStatus status = enqueue(s, s->batch, count);
        if (status != POCKET_IPC_OK) return status;
    }
    State *state = &s->states[slot >= 0 ? slot : empty];
    if (remove) memset(state, 0, sizeof(*state));
    else {
        state->used = 1;
        state->member = member;
        state->key = key;
        state->metadata = *metadata;
        state->length = (uint32_t)payload_length;
        if (payload_length) memcpy(state->payload, payload, payload_length);
        state->payload[payload_length] = 0;
    }
    s->revision = revision;
    ++s->state_updates;
    return POCKET_IPC_OK;
}

PocketIpcStatus pocket_service_state_put(PocketService *service, const char *collection,
                                         uint64_t key, const PocketIpcFilter *metadata,
                                         const char *payload, size_t payload_length) {
    Service *s = service ? service->impl : NULL;
    PocketIpcFilter empty = {{0}, {0}};
    if (!s || !key || !valid_payload(payload, payload_length) ||
        !pocket_ipc_filter_valid(metadata ? metadata : &empty)) return POCKET_IPC_INVALID;
    if (s->close_requested) return POCKET_IPC_DISCONNECTED;
    int member = find_member(s, collection, POCKET_SERVICE_STATE);
    if (member < 0) return POCKET_IPC_NOT_FOUND;
    return mutate_state(s, (unsigned)member, key, metadata ? metadata : &empty, payload, payload_length, 0);
}

PocketIpcStatus pocket_service_state_remove(PocketService *service, const char *collection, uint64_t key) {
    Service *s = service ? service->impl : NULL;
    if (!s || !key) return POCKET_IPC_INVALID;
    if (s->close_requested) return POCKET_IPC_DISCONNECTED;
    int member = find_member(s, collection, POCKET_SERVICE_STATE);
    if (member < 0) return POCKET_IPC_NOT_FOUND;
    return mutate_state(s, (unsigned)member, key, NULL, NULL, 0, 1);
}

int pocket_service_snapshot(const PocketService *service, PocketServiceSnapshot *snapshot) {
    const Service *s = service ? service->impl : NULL;
    if (!s || !snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->connected = pocket_ipc_channel_connected(&s->channel);
    snapshot->ready = ready(s);
    snapshot->broker_epoch = s->broker_epoch;
    snapshot->connection_epoch = s->connection_epoch;
    snapshot->epoch = s->service_epoch;
    snapshot->revision = s->revision;
    snapshot->requests = s->requests;
    snapshot->events = s->events;
    snapshot->state_updates = s->state_updates;
    snapshot->rejected = s->rejected;
    for (unsigned i = 0; i < POCKET_SERVICE_STATE_LIMIT; ++i) snapshot->state_count += !!s->states[i].used;
    for (unsigned i = 0; i < POCKET_SERVICE_WATCH_LIMIT; ++i) snapshot->subscription_count += !!s->watches[i].used;
    return 1;
}

void pocket_service_close(PocketService *service) {
    Service *s = service ? service->impl : NULL;
    if (!s) return;
    if (s->in_pump) { s->close_requested = 1; return; }
    service->impl = NULL;
    disconnected(s);
    memset(s, 0, sizeof(*s));
    free(s);
}
