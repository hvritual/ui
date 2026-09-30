# Framework-to-board integration (#49)

## Scope and architecture

The native Pocket reference application uses the existing Object, Layout/Style,
Component, Navigation, Overlay, Reactive, Model and Interaction contracts. It
projects an engine-neutral bounded scene resource through EngineApi v1. A private
adapter maps it to the **pinned real QuickJS/PocketJS software renderer**. No
application code depends on renderer property IDs or Linux device APIs. LVGL is
not selected or migrated by this change; #33's final comparative decision remains
open. The renderer choice does not change the public component semantics.

Pipeline: live P3 InputFrame -> F6 -> queued app actions -> reactive flush ->
layout/style projection -> EngineApi -> private retained rendering scene ->
Pocket software pixels -> existing fbdev Presenter. Guest/Core logic runs at
60 Hz; optional presentation is 30 Hz and zero-damage frames are skipped. This is
not an end-to-end damage optimization or a physical FPS guarantee.

Application: Home -> Detail -> confirmation Modal -> simulated Making -> Success.
Back/cancel, six fixed P4 display locales, two themes and horizontal card paging
share the same state. The standard workload has 8 drinks; `--items 100` is a
separate list-stress fixture. The viewport materializes at most two adjacent pages (12 card roots).
The 8-drink workload has 33 live Home component nodes; the 100-item workload
has at most 45, including the Text input entry. This replaces the old six-root, release-only page switch. The 600/800 layouts are computed independently, not
rescaled screenshots. A separate local ASCII Text input overlay is now integrated (see ascii-keyboard.md).
No offline IME, payment, hardware command, video, or final public TS SDK is added. No actuator, control/network or OTA process
is modified. This is not the complete coffee-machine product UI.

Scene limits: 256 records, 20512-byte private binary wire, finite stable IDs and bounded
traversal. Text/image references use the existing P4 catalog and prepared assets.
Unsupported component/visual properties fail explicitly. The current visual
subset covers View/Text/TextField/Image/Button/Progress/Grid/List/Scroll and blocking
overlays. Arbitrary fonts, shaping, image opacity, border strokes and transforms
are not silently accepted. Valid scene updates are applied on the UI thread;
malformed updates leave old state. Runtime allocation/JS failure stops the host;
it is not a claim of transactional recovery after OOM (P7).

## Software checks (cloud)

From a committed clean checkout, first build the existing P4 Native and ARM
runtime/assets. The P4 workflow performs those prerequisites and old regressions.

```sh
make test-framework
make test-framework-arm
make test-framework-sanitize
make verify-framework
make build-board-framework
make test-board-framework-package
make test-board-framework-verifier
```

`test-framework` executes the real production C/JS renderer, two viewports,
18 baseline plus 8 in-motion full-frame images, 16 synthetic keyboard images and
6 numeric replay logs, then repeats them byte-for-byte.
It runs an intentionally failing assertion, production headless CLI on both
profiles, refusal without an explicit physical/headless mode, rejection of `/dev/null`, refusal of
unverified 800 input defaults and refusal to overwrite evidence. The test suite
uses production InputFrame delivery, not real kernel devices. It covers modal
blocking, reactive progress with an independent pixel oracle, 24 navigation/
create/destroy cycles per viewport, 100-item bounded recycling, cancellation,
actual local media store updates, busy deferral, corrupt packet retention and
rollback. Media tests write trusted local store fixtures; signature/HTTPS/USB
installer tests remain the existing P4 tests, not fabricated network evidence.

A second test invokes the actual production CLI/main loop with only device I/O
and time replaced. It exercises UNBLANK, poll HUP, reconnect, SYN_DROPPED and
navigation through real F6/app/rendering code. Its report and startup label are
explicitly synthetic and cannot satisfy the hardware gate. Simultaneous pointer
releases consume at most one navigation action per snapshot.

Native/ARM actual pixels and numeric replay must match. The static ARM deployment
ELF itself is checked for ARMv7 hard-float, absence of INTERP/NEEDED and absence
of test-wrapper symbols, then run in Cortex-A7 QEMU. Build/test/source/asset/ELF
hashes are bound to one commit. Neither QEMU nor native timing is a board metric.
The CI-required Native sanitizer instruments this integration's C sources; linked
prebuilt third-party objects are not retroactively sanitizer-instrumented.

For explicitly identified local recovery development only,
`FRAMEWORK_DEVELOPMENT=1` allows using already verified unchanged dependency
objects. Such results are stamped development=true and **cannot** pass the
verification/package commands. Official CI rebuilds dependencies on its exact
commit; development mode is not enabled in the workflow.

## On the isolated 1024x600 test board

Copy and unpack `imx6ul-coffee-framework.tar.gz` to an approved writable test
location. Do not overwrite the existing UI. Retain its normal recovery procedure.
The operator must stop only competing UI/display/input consumers, leaving device
control, network and OTA services running. The script does not stop them itself.
Do not use this diagnostic on a machine serving customers.

```sh
cd coffee-framework
./run-framework.sh imx6ul-1024x600 180
```

Input defaults now select a **unique** known controller (`ilitek_ts` or
`goodix-ts`) by name and required capabilities, never a fixed event index.
ILITEK retains the historical Protocol B path. Slotless Goodix uses the new
tracking-ID Protocol A packet parser; Goodix exposing slots uses Protocol B.
The driver supplies independent X/Y ranges and, only for B, hardware slot count.
They are queried and checked on the board, not guessed from display resolution.
Explicit `--raw-min/--raw-max` and `--slots` remain optional constraints; a slots
constraint is invalid for A. Ambiguous/unknown controllers and invalid capability
or axis results are rejected. Reconnect rechecks identity, protocol and ranges.

Display geometry/stride/bounds checks remain active; no mode set, PAN or VSync is
enabled. The physical runner can UNBLANK the framebuffer. Exit leaves the last
screen and does not restart the old UI. Default direct axes are a diagnostic
candidate, **not verified orientation for Goodix**; record edge/asymmetric touch
checks before accepting a transform.

During the run: page forward/back and swipe; select a drink; open confirmation;
tap background buttons and verify no action; cancel/reopen; start and watch
progress complete; return home; switch locale/theme. Record actual LCD and finger
motion, not only a software screenshot. Rapid repeated input and cross-card drag
must not trigger duplicate/incorrect actions. A separate stress run uses
`FRAMEWORK_ITEMS=100` with the same script.

After the first run, return the complete printed LOG_DIR: startup.log, runtime/
report.json, display.json, timeline.csv, first.ppm, last.ppm and SHA256SUMS, plus
an actual LCD/touch video. Automatic `visual_validated` stays false; human review
is external and must not rewrite the original report. CSV includes app page,
modal, progress, first virtual index, selected index, locale and theme.

## Dynamic images using the existing trusted installer

The default test store is `coffee-framework/media-store`; it is local owner-only
storage. Override `FRAMEWORK_STORE_DIR` only with an approved writable directory.
A ZIP's supplied key is never implicitly trusted; use the matching public key
already included in this particular diagnostic bundle. No private signing key is
included. Use P4's signing workflow with an authorized key for your own images.
From a second terminal while the UI runs:

```sh
./mediactl install ./media-store ./updates/demo.public ./updates/demo-a.zip
./mediactl install ./media-store ./updates/demo.public ./updates/demo-b.zip
./mediactl rollback ./media-store ./updates/demo.public
```

Copying a signed file from USB and verified HTTPS fetch are installer transports,
not direct UI network execution. Use `mediactl`'s existing documented fetch syntax
and a validated CA/clock for real HTTPS; this task does not assert an endpoint was
contacted. Updates defer while pressed, in Detail/Modal/Making/Success, then apply
on idle Home. Bad updates retain displayed images. This test directory is not a
P7 crash-consistent production release or persistence approval.

## Independent 1024x800 board

800 has its own profile but no inherited hardware acceptance. Physical startup
requires an explicit `--touch-name` and `--swap-xy`, `--invert-x`, `--invert-y`
values from that board's approved orientation check. Protocol and independent
axis ranges are probed; numeric constraints are optional.
Do not copy 600 values just to make admission succeed. The existing P3 probe may
be used to collect those values before running this new package.

## What closes #49

Only returned evidence for **both** physical targets, a human review bound to the
exact package and LCD video, actual full navigation/virtualization/media behavior
and input fault-recovery checks can close #49. Software PASS and package readiness
do not close it. The checker intentionally rejects headless/synthetic reports,
missing video hashes, missing fault evidence and stale commits. It checks evidence
consistency, not authenticity of a dishonest operator; human review is mandatory.
A controlled disconnect/SYN_DROPPED procedure belongs to isolated board testing,
not an automatic command against a production touchscreen.

```sh
make verify-board-framework REPORT=/path/to/returned-evidence
```

Arrange each target's returned runtime directory under its profile name and add a
`review.json` using the schema described in `scripts/framework_hil.py`. No approved
review is generated by the build. Keep #49 open when any target/evidence is missing.
P4A input methods, P5 measured performance, P6 real machine IPC and P7/P8 production
recovery/long soak remain separate. Static linking is a diagnostic ABI choice,
not certification of every vendor uClibc BSP.


## Input discovery repair after the first F6A board run

The reported `ea27d76` binary hash matched the delivered ELF, but startup exited
with `INPUT_DEVICE_NOT_FOUND`. That message was not sufficient to identify the
physical cause: the old scanner replaced name/capability/profile/stat/resync
failures with the same generic error. No touchscreen failure or specific BSP
incompatibility is established by that log alone.

Discovery now preserves the highest-information rejection (a matched touchscreen
first), its errno, actual device name, queried capabilities, slot range and X/Y
ranges alongside the expected profile. `INPUT_CANDIDATE` JSON lines in
`startup.log` record present/inaccessible nodes without printing every absent
index. `runtime/input.json` records startup admission; `report.json` includes
`input_errno`, `input_discovery_attempts` and `input_wait_ms`. The runner also
captures the input directory, `/proc/bus/input/devices` and sysfs device names.
These operations do not modify the input driver or collect user text.

Startup waits at most 3000 ms for device appearance, with bounded 500 ms retries.
Override with `--input-wait-ms 0` (one scan) through `--input-wait-ms 10000`.
Permissions, capabilities and profile mismatches on a named touchscreen remain
fail-closed; neither an arbitrary event node nor a guessed coordinate range is
accepted. Blocking input-wait errors are retained for the next loop iteration,
so a transient terminal result cannot be discarded by a second poll.

Validation adds actual scanner/open/ioctl/resync rejection fixtures, a preserved
expected/observed report and JSON escaping check, aliased config re-open, delayed
device admission, bounded timeout, immediate permission/profile rejection, and
zero-wait behavior. The existing nine P3 test groups are retained; additional
rejection checks print CHECKED markers. Syscall fixtures and Native/ARM tests
are not physical evidence. Root-cause confirmation for this particular board
still requires the new startup.log plus runtime/input.json/report.json.

Unpack a repaired bundle in a new approved test directory; never mix its binaries,
resources or demo signing public key with an older bundle. Keep the original
failure log for version comparison. No system clock, kernel, libc, unrelated
service, actuator or display-mode change is part of this repair.


## Startup CLI convention

The legacy magic confirmation string is retired from the F6A and subsequent
startup path. User-facing startup is now:

```sh
./run-framework.sh imx6ul-1024x600 180
```

The runner selects the binary's explicit `--physical` mode internally.
Direct binary execution must choose exactly one of `--physical` or
`--headless`; neither implicit framebuffer writes nor a magic-word token are
accepted. Existing profile, framebuffer, input admission, package hash and HIL
checks remain unchanged.


## Goodix slotless input repair (2026-09-30)

Historical #5/#22 and `myimx6ek140-input-20260922.json` document **ilitek_ts**,
0..16384 on each axis and B slots 0..9. They are not Goodix measurements.
The returned `goodix-input-admission-20260930.json` establishes BTN_TOUCH,
MT tracking ID and X/Y capabilities but **no ABS_MT_SLOT**. Its axis fields
were unqueried, so neither 16384 nor ten slots can be reused as Goodix facts.

A name override alone was insufficient: the old live reader unconditionally
required B slots and called EVIOCGMTSLOTS. This repair adds a real Type A reader:
SYN_MT_REPORT ends a contact packet, SYN_REPORT commits the complete contact set,
tracking ID zero is valid and reordered packets retain logical pointer identity.
Missing fields, duplicate IDs and malformed frames never become successful taps.
The 32-contact parsing budget and eight-runtime-contact overflow policy are
explicit software budgets, **not a claim that Goodix has 32 or eight slots**.

A has no B slot snapshot. Startup/after SYN_DROPPED query EVIOCGKEY for BTN_TOUCH;
held contacts remain suppressed until release. Dropped input is ignored through
the next SYN_REPORT before re-arming. Reconnect re-probes without taking over
another controller or silently changing calibration. B keeps its existing slot
resync and all old tests. No driver replacement, system clock, kernel, libc or
machine service changes are required.

The syscall fixtures match the uploaded capability shape; their asymmetric
0..1023 / 0..599 ranges are explicitly **synthetic test inputs**. Tests cover
zero ID, reorder, movement/release, held startup, overflow, duplicate/incomplete
packets, SYN_DROPPED, reconnect, ambiguity and failed queries. A raw Type A trace
also drives the real Framework/F6/Core Coffee flow on both software viewports.
These tests cannot establish the current board's actual event trace or orientation.

Run on the isolated 600 test board without the former confirmation string:

```sh
./run-framework.sh imx6ul-1024x600 180
```

The startup record now includes `protocol`, `axis_source`, `slot_range_present`,
actual axis bounds and `contact_capacity`. Keep the exact new package, input.json,
report.json and LCD/touch evidence together; #49 still needs physical acceptance.


## Direct horizontal manipulation after the Goodix return

The returned 88a0da6 board report and operator feedback established visible,
working Coffee navigation on the Goodix 1024x600 board. The exact measured raw
bounds are X 0..1024, Y 0..600, MT-A. The original app nevertheless ignored
SCROLL_UPDATE: it changed pages only on SCROLL_END. This was not evidence of a
slow touchscreen. The mean 8.315159% CPU from a 177.266601-second mixed session
is neither idle CPU nor an active-scroll benchmark. Zero drops/reconnects/media
updates cannot prove those fault/asset gates. Original report flags stay intact.

The pager now tracks the pointer's original X after F6 wins horizontal slop,
uses logical-pixel offsets for both layout/hit testing and scene paint, keeps
an adjacent page available, and settles with a bounded 180 ms cubic ease-out.
Page stride is 984px (960px viewport plus 24px gutter). Boundary overscroll is
resisted and capped at 80px. A quarter-page displacement or a recent >=600px/s
flick over at least 48px selects the neighbor; stale velocity after a pause does
not select a page. A new press interrupts settling and does not activate a card.
Cancel restores the previously settled page; drag never creates a drink command.
The More button retains instant cyclic paging; swipes stop at either end.

Only the permanent scroll container owns an active drag; recycled card identities
cannot steal it. Viewport updates run after event dispatch. Idle turns do not
repeat application layout. Retained engine wrappers reassert clipping during
restyle; tests assert every pixel in the outer margins remains unchanged during
motion, as do the header/footer. Existing image textures are reused, not uploaded
or rebound every movement. No BSP, renderer revision, display mode, touch axis,
VSync or machine-control change is included. The existing 30Hz presentation
schedule is retained; no 60fps or photon-latency claim is made.

The two-page pool bound intentionally replaces the previous six-card bound.
Small eight-item catalogs fit entirely in that bound and need not recycle on
every page; the independent 100-item test proves bounded recycling. The hardware
verifier keeps its existing 64-node bound and requires recycling for larger
catalogs. These are explicit workload changes, not a relaxed hardware pass.

New timeline columns: scroll_x, dragging, settling; present_scroll_x,
present_dragging, present_settling; present_complete_ns, update_duration_ns,
render_duration_ns, present_duration_ns. Duration fields are monotonic elapsed
wall times around update/render/CPU framebuffer submission, not CPU-utilization
samples or scanout/photon latency. They describe the LAST successful present:
deduplicate rows by presents before computing intervals or percentiles. The
report adds motion_presents and layout_runs. Empty/idle intervals must not be
reported as animation FPS. Package software checks cannot substitute the board.

Retest with the same clean command, initially with all fingers released:

```sh
./run-framework.sh imx6ul-1024x600 180
```

Drag slowly in both directions before releasing; verify content moves before UP,
the neighbor appears inside the clipped viewport, short drags return, long drags
settle, and a new press stops settling without selecting a drink. Then confirm
normal tap navigation and return the complete log directory and LCD/finger video.
#49 stays open for the remaining separate target, asset and fault evidence.

## Frame path performance follow-up

The private adapter uses bounded binary scene records; input and app semantics
are unchanged. Scene transfer is coalesced at paint opportunities, and fbdev
uses validated damage after its initial full frame. Always install the matching
complete ELF/resource bundle. `report.json` includes actual framebuffer bytes,
partial-present count and scene-wire traffic. See `frame-path-performance.md`
for returned timing, test scope and the distinction from physical P5 acceptance.


## Local text input

Home now exposes `Text input`. English printable ASCII, unsigned integer digits,
Password and PIN are local test fields only. Confirm and Cancel return to Coffee;
no device setting is changed. See `KEYBOARD.md` in the bundle or
`docs/ascii-keyboard.md` in the repository for semantics and test scope.

Use synthetic values for review. All automatic screenshots are suppressed while
the editor is open and until its last rendered frame has been replaced. If still
open at timeout, no `last.ppm` is written; `FRAMEWORK_SNAPSHOT_SUPPRESSED` is logged
and the process can still exit successfully. The runtime report records only
open/confirm/cancel counts. Close the form before the normal Coffee/HIL evidence
run completes. No user text or password is part of diagnostic evidence.
