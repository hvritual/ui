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
