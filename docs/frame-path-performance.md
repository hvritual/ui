# Frame path optimization from returned board timing

Scope: #49 integration follow-up; not P5/physical performance acceptance.
Baseline: `b0d8cddf4811761c3f5bc7a8180aab4c4746fde1` (PR #54).

## Measured basis

The returned 600/Goodix mt-a run completed 180.094835 s, observed all four pages
and one simulated completion, and exited without input/display cleanup errors.
Raw-file hashes and untouched reports are in
`evidence/scroll-timing-return-20260930.json`.

`python3 scripts/analyze_motion.py RETURNED_RUNTIME_DIR` deduplicates the CSV by
`presents`. There are 8032 CSV rows, 901 distinct recorded presents, 673 motion
samples (581 dragging / 92 settling), plus the final forced present outside CSV.
For those 673 motion samples, renderer elapsed time P50=39.878667 ms /
P95=49.994333 ms; framebuffer present elapsed time P50=8.977333 ms /
P95=9.966800 ms. Update P50=0.639000 ms / P95=27.190666 ms is only the recorded
paint tick's update, NOT all work since the previous present: input callbacks and
skipped ticks are excluded by the old measurement. Do not sum it as complete
input-to-display latency. Mixed-session CPU36.775069% and RSS5788KiB do not prove
an active-motion utilization target or a regression against the quieter old run.
No frame-per-second number is derived from idle session duration.

## Changes

1. The private scene adapter now transfers bounded PSC1 little-endian numeric
   records rather than converting numbers to JSON text and parsing/stringifying
   them again. This is not a change to the public EngineApi, app semantics or an
   external package format. Header32bytes + 80bytes/record, max20512bytes. Stable
   IDs retain53bits, geometry/value fields are signed32, colors/refs unsigned32;
   no native padding or pointer is serialized. C and JS validate their respective
   boundaries. Decoder rejection precedes all mutations. Unknown, truncated,
   oversized and invalid field packets cannot partially change a frame.
2. App/input/guest ticks remain60Hz and paint opportunities30Hz. Intermediate
   non-paint ticks no longer project/transfer a scene the screen cannot display.
   Every input frame is still processed, including down/up/cancel. A forced frame
   flushes the newest state. Media-store polling only follows fresh scene upload,
   preserving busy-state admission and deferral.
3. fbdev uses the renderer's validated damage rectangle after the initial full
   frame. A missing damage rectangle/forced clean frame uses the existing full
   copy. A partial copy without an initialized framebuffer is rejected. All
   source/mapping/stride/channel/alias checks precede writes; framebuffer ownership
   and mode checks are still performed on every present. The original full-copy
   entry is retained for legacy diagnostics. RGB565 and all admitted32bit formats
   retain their exact channel/opaque-alpha behavior. No display mode/PAN/VSync,
   driver, input calibration, renderer revision or machine service change.

## Verification

Original26frame images and6replay logs stay byte-identical to the accepted
baseline under the same event sequence. Additional tests include:

- Real C/JS decoder, hand-encoded high-ID and negative-geometry packet,12 malformed
  packets rejected with the old pixel buffer unchanged.
-40input moves retain41ticks but at most21scene uploads; final forced frame agrees
  with current app geometry. This is coalesced rendering, not dropped input.
- Every integration/scroll present is applied to a shadow framebuffer using only
  the reported damage; its entire contents must equal an independent full copy.
- Partial copy byte-order/RGB565/tail-block, offsets/padding, invalid rectangles,
  short sources/maps, aliasing, first-frame baseline, actual written bytes and
  mode-change rejection. All previous presenter cases remain required.

Native recovered-dependency runs are explicitly development-only, not exact-head
CI acceptance. The formal existing workflows rebuild pinned dependencies, require
Native/ARM/repeat/sanitizer equivalence and test the actual static ARM package.
Native/QEMU elapsed timing is not a measurement on the user's board.

## Field test / limits

Use the complete rebuilt package; the scene wire change requires its matching
`framework.js` and ELF. Do not mix old/new resource files. The normal command is:

```sh
./run-framework.sh imx6ul-1024x600 180
```

Repeat slow drag/pause/reverse, release/snap and the normal Coffee flow. Return the
whole LOG_DIR. `report.json` adds `scene_wire_bytes`, `scene_uploads`, `scene_skips`
and `partial_presents`; physical `bytes_written` counts actual dirty area bytes.
Software equivalence and reduced operations do not establish30FPS. The real Core
raster stage still exists and may remain the dominant cost. Original current/last
presented scroll fields and stage clocks remain; need the new board run before
claiming an improvement in frame time or smoothness. #49 must remain open without
independent800target, media/fault and required human evidence.
