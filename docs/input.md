# P3 evdev input

Status: real 1024×600 capability admitted; Protocol B state/replay implemented in P3-01; physical event ordering and coordinate transform require a known-point trace before PocketJS delivery.

## Real 1024×600 admission

The board probe on 2026-09-22 established:

- `/dev/input/event0`: `20cc000.snvs:snvs-powerkey`; EV_KEY only; not a touch device.
- observed `/dev/input/event1`: `ilitek_ts`; EV_KEY + EV_ABS + BTN_TOUCH.
- `ilitek_ts` exposes both legacy `ABS_X/Y` and Protocol B `ABS_MT_SLOT/TRACKING_ID/POSITION_X/Y`.
- raw X/Y ranges are 0..16384.
- `ABS_MT_SLOT` is 0..9: ten hardware slots.

The event path is evidence, not identity. Runtime selection must match device name plus required capabilities rather than hard-code event1.

Evidence: `docs/evidence/myimx6ek140-input-20260922.json`.

## Protocol choice

Protocol B is preferred because the real device exposes slots and tracking ids. Legacy `ABS_X/Y + BTN_TOUCH` is a fallback only.

The host tracks all ten hardware slots. PocketJS currently accepts at most eight active contacts. If the hardware exceeds that budget, P3 cancels all contacts already published to the guest and suppresses new contacts until the hardware reaches all-up. It does not promote finger 9/10 after another finger lifts.

## Event semantics

- Accumulate kernel events until `SYN_REPORT`; only then commit a coherent input frame.
- A normal Protocol B release (`TRACKING_ID=-1`) becomes absence from the next active-contact snapshot.
- `SYN_DROPPED` emits terminal cancellations for contacts already visible to the guest, rejects unreliable intermediate state, requires kernel resynchronization, and suppresses new presses until all-up.
- Device removal/read failure/focus loss/shutdown must cancel visible contacts and clear capture/pressed state.
- Reusing a slot with a new tracking id cancels the old published contact before the replacement may enter.
- Physical keycodes are not Unicode. P4A owns keyboard layout, composition, candidates and committed text.

## Coordinates

The display is physically accepted at 1024×600 with rotation 0, but that does not prove touch-axis orientation. The raw ranges are admitted while `swap_xy/invert_x/invert_y` remain pending.

Mapping is a pure 64-bit transform:

`raw min..max -> logical 0..width-1 / 0..height-1`

with explicit swap/invert and edge clamp. A known-point trace is required before the transform is admitted.

## Trace evidence

`contracts/input-trace.schema.json` contains only numeric evdev `type/code/value` plus monotonic timestamps. Production text/password data is never serialized.

The P3-01 device package re-probes the candidate, hashes the exact capability JSON, and captures a bounded trace. On the real device run:

```sh
./run-input-trace.sh /dev/input/event1 15000
```

During the 15-second window perform, in order:

1. tap the top-left;
2. tap the center;
3. tap the bottom-right;
4. drag from top-left to bottom-right.

Return the generated `logs/trace-*/startup.log`, `capability.json`, and `input-trace.json`.

## P3 sequence

1. capability/device admission — **1024×600 complete**;
2. Protocol B state machine/recovery — P3-01;
3. trace capture/replay and transform admission — P3-01;
4. live non-blocking evdev read loop + kernel resync;
5. down-edge `pocket_runtime_hit_test_bounds` fact capture;
6. `pocket_runtime_tick_contacts` delivery;
7. input→guest→present timestamp evidence for P5;
8. separate 1024×800 physical admission.

P3 does not implement an IME or general gesture library.
