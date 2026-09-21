# P3 evdev input preparation

Status: preparation only. No physical input device has been admitted yet.

## Known display facts

The 1024×600 physical display path is visually accepted with rotation 0. This does **not** prove the touchscreen uses the same raw coordinate range or event node. The repository must not assume `/dev/input/event0`.

## Device admission

P3 will select devices by evdev capabilities and stable metadata, then record the selected node as evidence. Candidate classes:

- single-touch: `EV_ABS + ABS_X + ABS_Y`, with an explicit press/release source such as `BTN_TOUCH`;
- multitouch Protocol B: `ABS_MT_SLOT + ABS_MT_TRACKING_ID + ABS_MT_POSITION_X + ABS_MT_POSITION_Y`;
- physical keys: `EV_KEY`, kept separate from text/IME.

Unknown or incomplete capability sets are rejected rather than coerced into a supported protocol.

## Event semantics

- Accumulate kernel events until `SYN_REPORT`; only then commit a coherent pointer update.
- On `SYN_DROPPED`, cancel all active pointers and require state resynchronization before accepting another press.
- Device removal, read error, invalid tracking id, focus loss or shutdown must clear pressed/captured state.
- Coordinate normalization is a pure transform using the admitted ABS min/max, viewport, rotation and edge clamp.
- Physical key codes are not Unicode text. P4A owns layout/composition/candidate/commit behavior.
- Input consumed by a future soft keyboard/candidate layer must not click through to background drink controls.

## Trace evidence

`contracts/input-trace.schema.json` stores only raw numeric evdev events and timestamps. Production text/password content is never serialized into traces. Real-device traces must be explicitly marked `real-device-redacted` and bound to a capability hash.

## P3 implementation order

1. capability probe and device selection;
2. trace capture + replay fixture;
3. single-touch state machine;
4. Protocol B state machine if the real device requires it;
5. coordinate normalization and display mapping;
6. disconnect/`SYN_DROPPED` recovery;
7. PocketJS pointer/key delivery;
8. input→guest→present timestamp evidence for P5.

## Required physical evidence

When the P3 probe package is available, collect for every `/dev/input/event*` candidate:

- device name and input id;
- EV/KEY/ABS capability bitsets;
- ABS min/max/fuzz/flat/resolution for relevant axes;
- a short redacted trace containing one tap, one drag and one release.

Until those facts exist, `targets/input.json` deliberately keeps protocol, node and ABS ranges null.
