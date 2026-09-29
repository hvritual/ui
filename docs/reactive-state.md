# Pocket Reactive State v1

Issue: #38.

F5 uses an explicit dependency graph rather than implicit runtime dependency
tracking. Signals and computed nodes are generation handles. Computed
dependencies are bounded to 16 and rewiring performs cycle detection before the
graph is modified.

Updates are not rendered immediately. Signal mutation only changes reactive
state; `pocket_reactive_flush()` is the frame-boundary authority that
recomputes computed nodes, runs effects and applies component bindings.

Batch applies signal values immediately but blocks flush until the outer batch
ends. Transaction stages signal values and either commits atomically into the
graph or rolls back without exposing staged values.

Effects may mutate signals, but work is bounded by the flush budget and each
effect may run at most 16 times in one flush. Self-sustaining feedback therefore
fails as budget exhaustion/cycle instead of becoming an unbounded render storm.

Component bindings cover styleRef, textRef, resourceRef, visible, disabled,
value and semantic states. A binding whose component was destroyed is removed
automatically at the next flush.

`PocketExternalStateAdapter` is deliberately one-way: an authoritative
external/IPC field can ingest a value into a signal, but F5 exposes no hardware
or device write callback. P6 remains authoritative for machine actions.


Effect and binding subscriptions use generation handles rather than naked slot
IDs. A stale unsubscribe handle cannot remove a newer subscription that reused
the same storage slot.
