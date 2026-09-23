# Render quality and idle presentation

Baseline: `ce7fbe097268ace20d2bfa3b0040b815a1196e59`. References #25 / #6 / #7.
This is a software correction, not an assertion of measured i.MX6UL CPU savings.
The user's earlier P3 trace has no process CPU measurements. Compare runs on the
same board, clock, workload and media generation; never compare QEMU wall time.

## Scope

Coffee keeps its eight drinks, locales, interactions, timing, modal, progress and
resource update checks. Guest turns still run at the existing logical 60 Hz;
render opportunities remain 30 Hz. Core already retains unchanged pixels. Only
Coffee's **presentation** now skips a rendered frame when both damage pixels and
bounds say it is clean and the destination has previously been presented.
First-frame, explicit force, pause/resume and cleanup remain full submissions.
P2/P3 diagnostic loops continue exercising their old unconditional submission
behavior. Reopening/replacing a display requires invalidating the presentation
state; this API does not grant framebuffer ownership. No shared external writer
is permitted. Mode/stride/size checks remain before every actual write.

The Presenter adds a BGRX32/BGRA32 64-word copy path after all existing checks.
It normalizes X=0 or A=255 exactly like the scalar implementation. Unaligned
source/destination rows use memcpy to local words; other channel layouts and
RGB565 retain scalar conversion. Source and target padding are not written.
This is **not end-to-end dirty-rectangle presentation**: a changed frame still
submits its full visible area. VSync, PAN, PxP and kernel/BSP stay unchanged.

## Pixel quality

Original diagnostic cup contours were binary at native resolution; the images
are now baked at 4x and integrated to the unchanged 256x128 deployment texture.
PNG/JPEG installer minification integrates source area; enlargement is bilinear.
Both operate in premultiplied channels and unpremultiply at the end, so hidden
RGB cannot leak into transparent edges. All arithmetic is integer and tested on
native and ARM. Decode/scale run in mediactl, never in the per-frame UI loop.

The baseline label atlas was ALREADY 8-bit gray, not monochrome. The updated
bake uses 4x coverage followed by Lanczos reduction to the SAME 22px logical
font, 32x36 cells, baseline, advances, density=1 and allocation cap. It does not
load a larger atlas or enlarge small text in the runtime. Actual Core tests
assert intermediate text coverage, not merely that a font file exists. Human
LCD review is still required: oversampling can trade hinting sharpness for
smoother diagonals and must not be described as a proven board fix without it.
Do not increase runtime atlas density without matching Core downsampling.
No source font or signing private key is shipped.

Previously installed .rgba generations are not silently reprocessed. Publish
and install a new signed media version to apply the new resampling recipe. The
new demonstration A/B bundles are regenerated together with their public key;
never mix bundles and public keys from different deliveries. Text/image output
hashes legitimately change; the scene and interactions remain unchanged. These
hashes identify a new visual baseline, not a relaxed performance workload.

## Measurement

Coffee JSON now includes `presentation.clean_frames_skipped`, `bytes_written`,
and `usage`: process user+system CPU microseconds, wall nanoseconds, Linux peak
RSS KiB, a validity flag, and average CPU percent normalized to one core.
The sampled interval starts AFTER initial boot/first presentation and ends at
cleanup. Peak RSS is process lifetime high-water, not a per-second current RSS.
Usage excludes separately invoked mediactl and other machine services.

CSV keeps prior columns and adds render begin/end, Core damage pixels,
`present_skipped`, actual submitted bytes, and cumulative UI CPU/wall snapshots.
CPU snapshots update every 60 logical turns, not every CSV row; select distinct
positive wall snapshots to compute interval percentages. Do not call unchanged
rows new samples. `present_*` are zero on skipped submissions, not zero-latency
LCD frames. CPU present end is still not the time light appeared on the LCD.

Use the normal run-coffee-demo.sh script three times with the same duration:
1. idle: leave homepage untouched;
2. interaction: select/cancel, language/page changes, complete simulated making;
3. media: install B, wait for MEDIA_APPLIED, then rollback and wait again.
Record the run type separately. Keep machine controls/network/OTA alive, pause
only the original display/input consumer. Supply logs and a screen photo at
normal scale plus a close-up. Include background service load and CPU frequency
when available. The first run should show almost all optional submissions
skipped, but the CPU percentage must be measured, not inferred from that count.
Use /tmp only for disposable test resources; production needs persistent storage.

## Later media capability boundary

Both standby fullscreen and embedded-business video are required. A subsequent
independent task will add a bounded native media surface behind a stable handle,
one framebuffer writer, touch-to-exit gesture consumption, UI-priority overlay,
and session identity to reject late frames. Images, scenes and playback control
remain distinct. No decoder, carousel or dynamic scene is implemented here.
Do not move per-video-frame pixels through the whole-generation image callback.
