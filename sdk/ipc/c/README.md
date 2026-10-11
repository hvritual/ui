# C service provider SDK

This is the initial C provider interface for the local Service IPC runtime.
The public provider declarations are in `hosts/linux/ipc/service.h`; shared
statuses, framing limits and exact metadata selectors are in `wire.h`.
Background processes may also use `client.h` as asynchronous clients. No UI,
QuickJS value, renderer handle, actuator API or business method is linked into
the provider SDK.

## Declare a service

Create a `PocketServiceMember` array containing Query, Command, Event and State
members. Query and Command members provide a handler; Event and State members
do not. Names are unique within the service. Set `socket_path`, `service`,
`members` and `member_count` on `pocket_service_config()`, then call
`pocket_service_open()`.

`open` copies configuration strings and member records. Handler context objects
remain the developer's responsibility and must live until `close`. The initial
wire supports a versioned service namespace such as `vendor.counter.v1`;
it does not advertise an independent service API-major negotiation field.

Pump the service from one owner loop with `pocket_service_pump(service, budget)`.
Registration completes asynchronously; `pocket_service_snapshot().ready`
becomes true only after all declared members have been acknowledged. Opening
and registration time out after five seconds while the owner continues pumping.
A running
router accepts newly named services and members without recompilation or
restart, subject to its configured peer and member capacities and trusted-UID
policy. The current broker policy is an explicit development trust boundary;
it is not per-method multi-tenant authorization or authenticated package admission.

`tests/ipc/service_fixture.c` is a complete separately built provider process:

```sh
service-fixture --socket /run/pocket-ipc/bus --service vendor.counter.v1
```

Its default members are `get`, `add`, `changed` and `counter`. The executable
accepts `--query`, `--command`, `--event` and `--state` to demonstrate registration
of new member names with the same binary. The socket directory must satisfy the
broker's ownership and permission policy. `SERVICE_READY` means registration is
complete; it does not certify hardware behavior or deployment suitability.

## Request handling and operation identity

Handlers receive borrowed payload bytes and an optional business idempotency
key. They must copy data needed after returning. Payloads are bounded UTF-8;
the transport and provider SDK do not parse JSON or validate a business schema.
Use `pocket_service_reply()` to return an ordinary result and
`pocket_service_error()` to return a typed expected failure. More specific
domain error codes belong in the payload.

A Command requires a nonempty wire idempotency key. The provider owns the
relationship between that key, the operation and its result. The SDK forwards
the key unchanged; it does not equate a request ID with an operation, perform
deduplication, or retry a command automatically.

The counter fixture accepts exactly `{"delta": integer}` with a value between
-100 and 100. The same key and delta return the stored receipt; the same key
with a different delta is rejected. Its 16 operation records are never evicted:
new keys fail before mutation once full. These records survive router reconnect
within the same provider process, but do not survive a provider-process restart.
A durable authority must couple its journal to its own action/transaction.

Handlers must finish promptly. The pump limits message and handler counts; it
cannot interrupt a blocking C callback. For a long operation, validate and
accept it with a stable operation ID, return an explicit accepted receipt, and
continue the work through the application's own job execution mechanism.
Publish progress through State and expose result lookup through Query. A
transport cancellation does not undo an accepted operation.

The current C client interface is asynchronous and owner-driven. Do not block
the owner that must receive its reply. The UI SDK provides `await` through the
Application VM; a separate C blocking convenience API is not part of this slice.

## Event and State behavior

`pocket_service_publish()` sends an Event with optional metadata. The v1 filter
is one exact metadata key/value equality; an empty selector matches all. It
does not execute remotely supplied code and does not implement general `AND`,
`IN`, range or arbitrary payload-field filtering. Event publication requires
completed registration. Events are ephemeral: queue rejection is observable to
the publisher, and no offline history/replay is implied.

`pocket_service_state_put()` maintains a bounded provider-owned collection with
a nonzero `uint64_t` item key. String business keys can be included in the
payload. Each item has one metadata pair. State can be initialized before
registration and retained while disconnected; it is exposed by the next watch
snapshot after reconnection.

A watch installs on the same owner that mutates the store. It atomically
enqueues `SNAPSHOT_BEGIN`, matching items and `SNAPSHOT_END`, all at one revision,
before admitting subsequent deltas. Entering the selector and changing a
matching item produce keyed upserts. Leaving the selector produces
`STATE_REMOVE` with `{"reason":"filter_exit"}`; removing the authoritative item
uses `{"reason":"deleted"}`. Changes to nonmatching items may skip source
revisions without implying transport loss.

Replacing a filter requires unsubscribe followed by a new watch ID and
snapshot. Queue capacity is checked before mutation: if all required updates
cannot be admitted, the operation returns `BUSY` and leaves the store and its
revision unchanged. An external authoritative source must retry its latest
projection or invalidate the connection and resynchronize; it must not ignore
the rejection and present an outdated projection as current.

## Limits and lifetime

| Resource | Initial bound |
| --- | --- |
| Declared service members | 16 |
| State items across collections | 16 |
| Active provider watches | 16 |
| UTF-8 payload | 768 bytes |
| Outbound frames per connection | 32 |
| Input frames/handler calls per pump | Explicit budget, 1 to 64 |
| Filter | One equality pair or match-all |

The provider reserves one outbound slot while running a request handler so
publications within the handler cannot consume its reply slot. Snapshot and
multi-subscriber mutation batches enqueue all frames or none. These limits are
deliberate first-slice bounds, not measurements of the board's capacity.

All API calls for one `PocketService` belong to one thread/owner; there is no
internal locking. Recursive pump and reconnect during a handler are rejected.
Closing from a callback invalidates further publication and defers freeing
until the callback returns. Closing removes subscriptions before freeing.

On disconnect, pending watch routes are dropped. The caller schedules bounded
retries through `pocket_service_reconnect()`, which preserves local state but
establishes a fresh session/service incarnation and re-registers members.
Treat the broker epoch together with the service epoch as the incarnation;
the service counter alone may restart when the router itself restarts.
Clients explicitly create new watches and consume fresh snapshots. Developer
services remain independent processes; closing a UI application does not kill
their process or delete their authoritative state.
