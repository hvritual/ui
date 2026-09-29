# Pocket Interaction Runtime v1 — core slice

Issue: #39.

This first F6 slice places a stable semantic layer above P3/Linux input:

evdev/P3 -> pointer events -> hit-test/capture/focus -> Object Model events

The runtime owns pointer lifecycle state for at most 8 simultaneous pointers.
Pointer down hit-tests the active scene. Move/up use explicit capture when
present; otherwise they hit-test the current location and fall back to the down
target. cancel_all is the single backend-loss path for SYN_DROPPED, disconnect,
or process-level input reset.

Hit testing is layout/clip-aware and descends the Pocket Object tree in visual
child order. If an Overlay captures input, only the topmost capturing overlay
root participates; background components are not hit.

Focus is a Pocket UI handle, not a Linux keycode or engine object. Programmatic
focus requires mounted+visible+enabled+focusable. Optional focus scopes constrain
focus to a subtree. Focus updates the semantic FOCUSED state and emits explicit
gain/loss events.

Key routing uses semantic PocketKeyAction values (Back/Accept/Next/Previous/
directions). Physical keycodes remain backend concerns.

Gesture recognizers and arbitration are the next F6 slice; this commit does not
yet claim Tap/DoubleTap/LongPress/Pan/Drag/Flick.


## P3 backend bridge

`hosts/linux/input/interaction_bridge.*` is the only F6 entry point that
depends on P3 `InputFrame`. It converts contact snapshots into semantic
Down/Move/Up/Cancel events and converts kernel monotonic nanoseconds to the
Interaction Runtime's millisecond clock.

Tracking ID `0` is valid. Contacts that hit no semantic target are tracked by
the bridge but are not injected into the Interaction Runtime, so a later move or
up does not create a phantom pointer slot.

`SYN_DROPPED`, suppressed frames and disconnect all invoke the same
`pocket_interaction_cancel_all()` path. A stale destroyed target also releases
its pointer slot immediately.
