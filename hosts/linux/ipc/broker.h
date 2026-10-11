#ifndef POCKET_IPC_BROKER_H
#define POCKET_IPC_BROKER_H
#include "wire.h"
#include <sys/types.h>

#define POCKET_IPC_PEER_CAPACITY 8u
#define POCKET_IPC_MEMBER_CAPACITY 64u
#define POCKET_IPC_ROUTE_CAPACITY 64u
#define POCKET_IPC_BROKER_SUB_CAPACITY 64u
typedef struct { void *impl; } PocketIpcBroker;
typedef struct {
    const char *socket_path;
    uid_t client_uid;
    uid_t service_uid;
} PocketIpcBrokerPolicy;
/* Explicit trusted-UID development policy enforced with SO_PEERCRED. No
 * client-supplied identity grants authority; no production tenancy claim.
 * Socket path must not already exist; never unlink an arbitrary old pathname. */
PocketIpcStatus pocket_ipc_broker_open(PocketIpcBroker *broker, const PocketIpcBrokerPolicy *policy);
PocketIpcStatus pocket_ipc_broker_step(PocketIpcBroker *broker, uint64_t now_ms, uint32_t message_budget);
int pocket_ipc_broker_fd(const PocketIpcBroker *broker);
void pocket_ipc_broker_close(PocketIpcBroker *broker);
/* CLI: pocket-ipcd --socket PATH --client-uid UID --service-uid UID */
int pocket_ipc_broker_main(int argc, char **argv);
#endif
