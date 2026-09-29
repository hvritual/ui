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
separate list-stress fixture. Both use six recycled card roots, with 26 live
component nodes on Home. The 600/800 layouts are computed independently, not
rescaled screenshots. No keyboard, editable text/IME, payment, hardware command,
video, or final public TS SDK is added. No actuator, control/network or OTA process
is modified. This is not the complete coffee-machine product UI.

Scene limits: 256 records, 64 KiB numeric JSON, finite stable IDs and bounded
traversal. Text/image references use the existing P4 catalog and prepared assets.
Unsupported component/visual properties fail explicitly. The current visual
subset covers View/Text/Image/Button/Progress/Grid/List/Scroll and blocking
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
make verify-framework
make build-board-framework
make test-board-framework-package
make test-board-framework-verifier
```

`test-framework` executes the real production C/JS renderer, two viewports,
18 full-frame images and 4 numeric replay logs, then repeats them byte-for-byte.
It runs an intentionally failing assertion, production headless CLI on both
profiles, refusal without write consent, rejection of `/dev/null`, refusal of
unverified 800 input defaults and refusal to overwrite evidence. The test suite
uses production InputFrame delivery, not real kernel devices. It covers modal
blocking, reactive progress with an independent pixel oracle, 24 navigation/
create/destroy cycles per viewport, 100-item bounded recycling, cancellation,
actual local media store updates, busy deferral, corrupt packet retention and
rollback. Media tests write trusted local store fixtures; signature/HTTPS/USB
installer tests remain the existing P4 tests, not fabricated network evidence.

Native/ARM actual pixels and numeric replay must match. The static ARM deployment
ELF itself is checked for ARMv7 hard-float, absence of INTERP/NEEDED and absence
of test-wrapper symbols, then run in Cortex-A7 QEMU. Build/test/source/asset/ELF
hashes are bound to one commit. Neither QEMU nor native timing is a board metric.
The optional Native sanitizer instruments this integration's C sources; linked
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
./run-framework.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER imx6ul-1024x600 180
```

Known default input selector is `ilitek_ts` plus Protocol B capabilities, not a
fixed event number. Raw bounds are 0..16384, 10 slots, direct axes for the
previously validated board. Any mismatch fails admission. Framebuffer is probed
at runtime; 32-bit layout, stride, bounds and ownership checks stay active. The
only new display ioctl is explicit UNBLANK after write consent. No mode set,
PAN, VSync or acceleration is enabled. Exit leaves the last screen; old UI is
not automatically restarted.

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
requires explicit `--touch-name`, `--raw-min`, `--raw-max`, `--slots`, `--swap-xy`,
`--invert-x`, `--invert-y` values from that board's approved capability/axis probe.
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
