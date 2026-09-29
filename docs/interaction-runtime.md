# Pocket Interaction Runtime

Issue #39; full-framework board integration remains #49.

## Ownership and dispatch

The Linux P3 InputFrame bridge is the only input-backend-dependent boundary.
Applications receive Pocket pointer/key/gesture events, not keycodes or native
engine handles. The owner calls input delivery, then `pocket_interaction_tick`
at a frame boundary. `pocket_interaction_next_deadline` permits bounded sleep;
a stationary page has no recognizer timer after all interactions finish.

There are at most 8 pointers, 128 generation-bound gesture registrations,
16 pending double-tap candidates, and 16 focus-history records. Hit testing has
a 256-depth / 4096-node work bound. All hot-path state is preallocated.
Registrations for destroyed handles are recycled; generations prevent reuse
from reviving old subscriptions. Overflow and reversed input clocks fail
explicitly rather than manufacturing events.

Raw down is hit-tested once. Move/up retain that target, or an explicit capture;
there is no move/up retargeting into an unrelated button. Layout clipping and
ancestor mounted/visible/enabled state are checked. Overlay interception limits
hit testing and capture admission to the topmost capturing overlay.

Callbacks can consume propagation or cancel the default gesture. They can
request capture/release and programmatic focus. Recursive pointer/tick/key
injection and scene switching from inside dispatch return BUSY. Application
navigation is queued and applied after dispatch. Destroying a target from a
callback is safe: it is revalidated before subsequent semantic delivery.
Disposal belongs to the outer owner; an attempted disposal inside dispatch
requests cancellation but does not free a runtime whose stack is active.

## Recognizers and arbitration

Clickable targets default to Tap; Scroll objects default to vertical Scroll
and Flick. `pocket_interaction_set_gestures` supplies an explicit per-target
mask; zero disables defaults. No application hardware operation is attached to
these events by the runtime.

- Tap: one event on release inside the original target, and no movement beyond
  12 logical pixels. Moving out and returning does not become a tap.
- DoubleTap: opt-in; a single is deferred for 300ms. Two qualified releases on
  the same live target within that interval and 24 pixels emit only DoubleTap.
  At the exact expiration boundary the first single wins. Opening an overlay,
  changing a page or canceling the input discards delayed singles.
- LongPress: opt-in; one event after 500ms of stationary hold. It suppresses Tap
  on release and never repeats. A cancel or slop-crossing move at the deadline
  wins over LongPress.
- Pan / Drag / Scroll: crossing slop selects the nearest eligible registered
  ancestor (Drag, then Pan, then axis-compatible Scroll at each node). An
  explicit child Drag wins over parent Scroll. A vertical-only parent cannot
  claim a horizontal gesture. A parent win sends cancel to the original child,
  owns capture, and suppresses the child's Tap.
- Motion has Begin/Update/End events with bounded integer deltas. Flick adds a
  final numeric velocity only when movement was within 150ms of release and
  at least 600 logical pixels/second. This is recognition, not an implicit
  inertial-animation engine. Applications decide how to use the event.

Events carry pointer identity, a full 64-bit effective monotonic timestamp,
position, deltas and velocities. No text is logged. Backend source timestamps
must not run backwards, but an already queued source event may precede the
last frame-clock tick; the effective dispatch clock never moves backwards.

## Cancellation and focus

SYN_DROPPED, suppress-until-all-up and disconnect enter one cancellation path.
A monotonic Overlay input epoch also detects open/close within a single frame.
Mid-gesture overlay/scene changes cancel old capture and recognizers; remaining
moves/up are suppressed, never injected as a new gesture into the overlay.
Destroyed, hidden or disabled ancestors revoke eligibility. The P3 bridge
validates all IDs, counts and duplicates before delivering any edge; an invalid
snapshot cancels the existing interaction and fails as a whole.

Focus is restricted to the active scene/top capturing overlay, FocusScope,
and a mounted, visible, enabled ancestor chain. Every semantic key revalidates
admission. Focus changes clear/set the semantic state and emit loss/gain events.
Nested modal/keyboard transitions preserve bounded per-root focus/scope history.
Dismissal restores only a still-live, currently eligible target; stale page
handles cannot receive a key. Text sessions and IME are owned by #12, not this
runtime. Key actions never imply Unicode text.

## Acceptance

```
make test-interaction test-interaction-bridge
make test-gesture test-focus test-interaction-replay
make test-interaction-arm
POCKET_INTERACTION_SANITIZE=1 ASAN_OPTIONS=detect_leaks=1 make test-interaction test-interaction-bridge test-gesture
```

`tests/ui/test_gesture.c` adds eight grouped suites: independent 600/800 edge
and cross-target behavior, timed recognizers, motion arbitration, nested
modal/keyboard and focus restore, cancellation/reentrancy/stale-target safety,
atomic malformed bridge rejection, and 1200 registration lifecycle cycles.
`tests/ui/fixtures/interaction.csv` is synthetic numeric replay, not hardware
trace. Two repetitions must match byte-for-byte, and ARM replay must also match
Native. An intentional assertion failure must exit nonzero. These tests are
functional evidence only: no LCD visibility, CPU/RSS or board latency claims.

## Integration boundary

F6 supplies reusable interaction semantics. F6A must still wire the actual
application, reactive flush, layout, engine adapter, presenter, text/image
resources and real input into one deployable UI process. A green F6 suite is
not evidence that the complete Framework Coffee application ran on a board.
