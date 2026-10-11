# PocketComm for the Application VM

This SDK aggregates Query, Command, Events and State for a packaged application.
It is part of the development integration described in
[`docs/service-ipc.md`](../../../docs/service-ipc.md). The UI owns one nonblocking
native client; background services and the broker run independently.

## Package and host setup

Bundle `pocket-comm.js` before your application's source, then append
`PocketComm.install(PocketApplication)`. The package still contains one
`application.js`; the runtime does not load arbitrary SDK files or service code.
The repository's `apps/ipc-demo/application.js` demonstrates this arrangement.

Declare each needed capability explicitly in the package manifest:

```json
"capabilities": ["ui.core", "ipc.query", "ipc.command", "ipc.events", "ipc.state"]
```

The new capability bits are 16, 32, 64 and 128. Legacy grants 7 and 15 do not
include them. Runtime/SDK API 1 remains supported; an older runtime rejects
capabilities it does not understand.

The host supplies the development socket. JavaScript cannot select a filesystem
path. The CLI requires an admitted package, `--headless` and an explicit
`--ipc-development-socket /absolute/path`. The current broker uses a trusted UID
policy; self-reported application IDs are diagnostic labels. This does not
provide production publisher authentication or isolation between applications
running with the same UID. Actual device service acceptance remains separate.

## Requests and ordinary results

```javascript
const comm = new PocketComm({
  onError(error) { model.callbackError = error.code; }
});

async function increment() {
  const before = await comm.query.call('vendor.counter.v1', 'get', {});
  if (!before.ok) { model.error = before.error; return; }

  const result = await comm.command.execute(
    'vendor.counter.v1', 'add', {delta: 1},
    {timeoutMs: 2000, idempotencyKey: model.operationKey}
  );
  if (result.ok) model.receipt = result.value;
  else model.error = result.error;
}
```

The application lifecycle methods `start`, `event`, `tick` and `inspect` remain
synchronous. Start an internal async workflow and handle its exceptions instead
of returning its Promise from a lifecycle method. Callbacks and Promise
continuations should update your JS model; the synchronous tick projects it
into UI components. `await` does not block the UI owner.

Requests resolve these shapes for ordinary outcomes:

```javascript
{ok: true, value: businessPayload}
{ok: false, error: {code: 'TIMEOUT', effect: 'unknown'}}
```

`effect: 'unknown'` means an accepted Command may already have affected the
business. Cancellation, timeout or disconnect cannot roll it back. Preserve the
same business operation key when resolving its outcome with the authority. The
SDK never retries an accepted Command automatically or invents an operation key.
The provider defines its receipt semantics and durability of deduplication.

Options accept only `timeoutMs` and, for Command, the required `idempotencyKey`.
Timeout defaults to 2000 ms and must be an integer from 1 to 120000. Its clock
starts on the first SDK tick after enqueueing, covering local queueing and the
handshake. `AGAIN` retries only an intention not yet accepted by native code.
An ordinary refusal resolves a Result. Query and Command Promises also expose
`cancel()`, which marks pending work for cancellation on a tick and returns
whether it changed the local lifecycle. A result already delivered cannot be
cancelled. Admission failures such as a full request quota may resolve immediately.

Payloads are JSON-serialized once and copied; later caller mutation does not
change a queued request. Serialization failure produces `INVALID`; over 768
UTF-8 bytes produces `TOO_LARGE`. A malformed provider JSON response produces
`BAD_PAYLOAD`. Native code transports escaped payload strings inside a trusted
JSON envelope; provider text is never evaluated as JavaScript.

## Events and State

```javascript
const odd = {eq: {key: 'parity', value: 'odd'}};
const event = comm.events.subscribe('vendor.counter.v1', 'changed', odd, result => {
  if (result.ok) model.lastEvent = result.value.value;
  else model.eventError = result.error;
});
const state = comm.states.watch('vendor.counter.v1', 'counter', odd, result => {
  if (result.ok) model.items = result.value.items;
  else { model.items = []; model.stateError = result.error; }
});
```

Both signatures accept an optional final `{timeoutMs}` argument. Their returned
handles expose `ready: Promise<Result>`, `close()` and `active`. `ready` resolves
on the Event subscription acknowledgment, or after the complete State snapshot
has arrived. `active` means the handle has not ended, including while it awaits
its first result. Closing suppresses subsequent callbacks and clears State
membership immediately; the tick sends the native unsubscribe.

| Callback | Successful `result.value` |
| --- | --- |
| Event | `{value: businessPayload, cursor, serviceEpoch}` |
| State | `{type, items, revision, serviceEpoch, key?, value?}` |

State `items` is an array of `{key, value}`. `type` is `snapshot`, `upsert`,
`remove` or `reset`. An upsert includes its item `key` and business `value`; a
remove includes its `key`. State `current()` returns a separate decoded copy of
the committed collection, so mutating it does not corrupt the SDK cache.
All wire IDs, versions and cursors use decimal strings, including values beyond
JavaScript's safe integer range. Do not convert them to Number.

The provider serializes snapshot creation and mutations on its owner. The SDK
stages snapshot items and replaces the entire committed collection at END.
The provider must emit remove when an item leaves the selector, including a
metadata change that stops matching. A full replacement removes any old items
absent from the new snapshot. A reset invalidates the collection and terminates
the old watch. For a bare successful reset, the callback reports `type: 'reset'`
with an empty collection and `active` becomes false; unfinished `ready` resolves
`STALE`. An error reset reports a failure Result. Explicitly create a fresh watch
to obtain a new snapshot.

The only filters are `null` (all) and one `{eq:{key,value}}` string equality.
Filtering uses provider metadata, not arbitrary fields extracted from JSON.
`in`, multiple conditions, comparisons or executable expressions return
`INVALID`. To change a filter, close the old handle and create a new watch.
Events have no historical replay guarantee; use State for a current projection.

## Lifetime, diagnostics and bounds

`comm.close()` ends that instance's pending requests and subscriptions. It is
idempotent. `PocketComm.reconnect()` explicitly recovers the one shared native
session, affecting **all instances**. Its result confirms the reconnect attempt,
not a completed handshake. Old work is invalidated, and subscriptions require
explicit creation. Correlation checks include broker epoch, connection epoch
and request ID, so retained messages cannot complete a new session's requests.
Application teardown closes the native client and clears its inbox before
destroying the VM.

Subscription callbacks may throw or return a rejecting Promise. These exceptions
are reported through the instance's optional `onError`, `lastError` and
`callbackErrors`. An exception in `onError` records `ERROR_HANDLER_ERROR`.
After an instance or subscription closes, late callback rejections are consumed
without invoking business error callbacks. Already started application async
code remains the application's responsibility.
Query/Command results are ordinary resolved Promises; exceptions thrown by your
own `.then` or async workflow still need their own error handler.

| Resource | Limit |
| --- | ---: |
| Active `PocketComm` instances per Application | 4 |
| Queued or accepted requests shared by all instances | 32 |
| Event and State subscriptions shared by all instances | 16 |
| Cached items per watch | 32 |
| Request/subscription submission attempts per tick | 16 |
| Native messages consumed per tick | At most 16 and at most 8192 envelope bytes |
| Business JSON payload | 768 UTF-8 bytes |
| Service, member and idempotency key | 63 ASCII characters each |
| Filter key / filter value | 31 ASCII characters / 95 UTF-8 bytes |

Names permit letters, digits, `.`, `_` and `-`. The native client has its own
bounded queues and capability checks. These SDK limits do not grant permissions
or replace the package heap budget. Native messages that do not fit the current
JSON batch remain queued for a later tick.
Cleanup controls are also bounded. If they exhaust their queue, the SDK retires
the shared session at an owner tick, resolves remaining work with structured
`BUSY` results (accepted Commands have unknown effect), and attempts a fresh
connection. It does not throw for cleanup pressure or replay accepted work.

`node tests/ipc/sdk.js` tests facade semantics with a controlled native fixture.
It is not runtime or socket acceptance. The repository's real packaged
Application VM and separate-process gate is `scripts/ipc_acceptance.py`.
