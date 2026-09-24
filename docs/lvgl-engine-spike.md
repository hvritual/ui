# F0 LVGL engine feasibility spike

Issue: #33. This is an isolated feasibility experiment, not an engine migration.

## Pinned upstream

The experiment pins **LVGL v9.6.0** at commit
`80ca777e37a2b176770726a02e07a6fb79ef0b39`. The implementation is kept behind
`experiments/lvgl/adapter.*`; `workload.c` is intentionally forbidden from
using `lv_*` symbols. A future Pocket public SDK must not depend on LVGL types,
handles or enum values.

The source lock and MIT provenance live in `toolchains/lvgl.lock.json`.
Do not update the tag/revision implicitly.

## Why CPU is a first-class gate

A low framebuffer write count does not prove a low CPU load. CPU can remain high
because of a hot timer loop, repeated invalidation/layout, input polling, bridge
churn or rendering work that is later discarded. Therefore this spike measures:

- process user+system CPU time relative to monotonic wall time;
- timer-handler calls and calls that produced no flush;
- no-timer returns and immediate retry ratio;
- event-loop wakeups/s and requested sleep time;
- post-warmup flush calls/pixels/bytes and full-screen flush count;
- Pocket-to-engine create/update calls and duplicate updates.

The loop is **deadline-aware**: it uses the delay returned by LVGL's timer
handler, clamps only for safety, and sleeps instead of busy-spinning. In a
production Linux host, the same deadline must become the timeout of the blocking
input/business poll so input wakes the loop early without a fixed 1 ms poll.

The initial P5 budget remains idle single-core CPU <10%. F0 also records <5% as
an observation target, not a production threshold. Native CI can detect obvious
busy-loop regressions but cannot certify the i.MX6UL. ARM/QEMU is functional
evidence only.

## Workload and current limitation

The F0 C workload mirrors the frozen Coffee screen's structural pressure:
1024-wide dual target, title/locale control, eight drink cards, labels and a
progress surface. The source hashes of the real P4 Coffee JS and locale file are
embedded in every report.

This first slice does **not** claim QuickJS-to-LVGL semantic equivalence, image
decode parity, Chinese shaping parity, touch-visible latency, or physical board
performance. Those remain open in #33. The experiment exists to establish the
engine boundary and catch the known high-CPU failure mode before a deeper
bridge is built.

## Gates

```sh
make check-engine-spike
make fetch-engine-spike
make test-engine-spike-native
make test-engine-spike-arm
make verify-engine-spike
```

Reports are written under `out/engine-spike/reports/{native,arm}`.
`comparison.json` must remain `PENDING_PHYSICAL_EVIDENCE` until both physical
1024×600 and 1024×800 targets have same-workload measurements.

### Idle gate after warmup

- no redraw pixels;
- wakeup rate <= 80 Hz;
- immediate retry ratio <= 20%;
- native regression guard <10% one-core process CPU.

The wakeup limit is deliberately diagnostic rather than a production target.
A periodic LVGL refresh timer can still wake an otherwise idle process; real
board data will determine whether that needs to be decoupled or coalesced with
the host event loop.

## Adopt/Reject rule

Do not adopt LVGL because it is mature, and do not reject it because the current
renderer already works. The decision needs the same P4 workload, same assets,
same input replay and same physical device measurements against #25.

If LVGL passes functionally but idle CPU is materially worse than the current
renderer, #33 remains failed or `Continue Investigation` until the source of
the excess CPU is isolated (timer loop, render, flush, input poll or JS bridge).


## Physical framebuffer slice

The CI/headless flush callback intentionally does no framebuffer I/O. It proves scheduler/render invalidation behavior only.

For physical evidence, the board package enables LVGL 9.6's Linux fbdev driver with:
- partial rendering;
- 40-row XRGB8888 buffer;
- mmap framebuffer writes;
- no LVGL VSync wait in this first comparison;
- an instrumentation wrapper around LVGL's real flush callback.

The executable performs a strict preflight before allowing writes: character-device major 29, exact expected resolution, 32-bpp packed true-color, RGB offsets 16/8/0, zero viewport offset and sufficient stride/memory. It holds an exclusive `flock` while running. This deliberately matches the already observed 1024×600 board and fails closed on unknown framebuffer layouts.

The physical report separates:
- total process CPU;
- LVGL timer/render handler CPU;
- real fbdev flush/copy CPU;
- bridge update CPU;
- flush pixels/bytes and wakeups.

The static ARM binary avoids a dependency on the board's reported uClibc loader; this is a diagnostic portability technique, **not** proof that a production dynamically-linked Runtime is ABI-compatible.

Build:
```sh
make package-engine-spike-board
```

Board:
```sh
./run-lvgl-fbdev-spike.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 1024x600 10000
```

The existing display owner must be stopped first. The test changes the visible framebuffer and does not restore the previous UI.
