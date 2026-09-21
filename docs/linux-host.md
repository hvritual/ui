# Linux Host: P1 implementation and acceptance boundary

## Implemented path

`bounded local JS/PAK → real pocket_runtime.c → real QuickJS → real ui-cabi/core → layout → DrawList → software BGRA framebuffer`.

This is a **headless native HostOps host**, not a Vue app, a browser, an official
PocketJS platform registration, an fbdev driver, an input driver or an IME.
The fixture uses actual native operations and exercises two logical viewports:
1024×600 and 1024×800. Rendering into allocated memory is not a screen test.

The target identifies itself as `linux-headless`, ABI 1, from the checked
`targets/linux-host.json`. The upstream compiler's platform catalog is unchanged;
packages built for another target must not be relabelled and assumed compatible.
P4 will integrate the component compiler/build plan with the actual Linux target.

## Build

P0 remains Rust 1.90.0 / GCC 11 and retains its independent smoke test.
P1 uses upstream's standalone `engine/ui-cabi` workspace, its Cargo.lock, and
`nightly-2026-07-02`, with `bare-platform,software-only,host-allocator`.
The compatibility archive name `libpocketjs_symbian_core.a` is not renamed.
No GLES symbols, desktop service, Node runtime, offload provider or actuator API
is added to the device host. The upstream abort-only Rust personality shim is
used with panic=abort; it is not an implementation of unwinding.

On the cloud Ubuntu 22.04 build environment:

```sh
sudo apt-get update
sudo apt-get install -y gcc-arm-linux-gnueabihf libc6-dev-armhf-cross binutils-arm-linux-gnueabihf qemu-user patch
rustup toolchain install 1.90.0 --profile minimal --component rustfmt --target armv7-unknown-linux-gnueabihf
rustup toolchain install nightly-2026-07-02 --profile minimal --component rust-src --target armv7-unknown-linux-gnueabihf
make check
make check-targets
make fetch-runtime
make test-runtime
make test-runtime-arm
make verify-runtime
```

Builds require a clean committed checkout. Sources are fetched by exact revision,
verified, and archived into disposable build trees. Only the hash-locked patch
is applied, with zero fuzzy matching; the upstream checkout is left pristine.
The GNU baseline is reproducible by source/version/configuration, but apt patch
versions and the runner image are not a hermetic vendor SDK. Binary ABI reports
remain authoritative; do not infer the device's libc from Buildroot's label.

## Host ownership and lifetime

`hosts/linux/host.h` is synchronous and single-owning-thread only. All host calls,
C ABI calls and allocation telemetry must stay on that thread; this API does not
promise thread safety. A second live host is rejected to protect upstream's
singleton. Each host is zero-initialized; close is idempotent. On a guest failure,
further turns are rejected until close/reopen. Boot errors release resources.

JS and optional PAK are loaded once. The PAK buffer remains alive until after
`pocket_runtime_shutdown`, because QuickJS borrows it. Only regular leaf files in
an explicit trusted asset root are read; traversal, absolute leaf paths, symlink
files, FIFO/device files, oversize files and truncated/grown reads are rejected.
The caller must choose a trusted root (parent path components are not a sandbox).
Limits are 1 MiB for source and 16 MiB for PAK. These are admission caps, not
measurements of production app size. No secrets or user input are logged; errors
are stable codes rather than upstream guest exception strings.

The returned pixel pointer is borrowed. Copy/use it before another mutating call;
do not retain it across render, resize, shutdown or init. Software output is opaque
top-left BGRA bytes, tightly packed at width×4. That is a **core buffer** contract,
not the device's framebuffer stride. P2 must query the actual display.

### Shutdown patch

At the pinned upstream commit, `ui_shutdown` calls `clear_framebuffer`, which
clears the Vec length but retains its allocation. `WRAP_BREAKS` is also global.
`patches/pocketjs/release-shutdown-buffers.patch` drops both buffers on shutdown;
viewport changes keep the original reuse behavior. The patch is limited to this
lifetime boundary and is covered by a render/shutdown loop that checks **zero
outstanding core allocator bytes/blocks after each of 100 iterations**.
This is not a full-process leak proof: telemetry excludes QuickJS and libc; RSS,
fragmentation, process OOM and long-duration behavior remain P5/P7/P8 work.

## Time, ordering and pause

The monotonic clock schedules 60 guest turns per logical second. Every turn calls
`pocket_runtime_tick` exactly once: guest frame → pending job drain → one core
tick. Rendering is separate, once per two turns (30 offscreen renders per logical
second). No legacy two-core-ticks-per-guest API is used. Instrumented tests assert
the stage order and actual Promise-driven layout/color changes. Production
binaries do not expose harness calls or stage hooks.

Pause stops turns; resume rebases deadlines without replaying suspended time.
Catch-up is limited to four full guest/core turns per pump. Excess wall-time debt
is explicitly dropped and counted in `overruns`; this may slow logical time under
load and is not a real-time guarantee. A backwards timestamp fails closed.
The CLI sleeps to absolute monotonic deadlines rather than busy-spinning.
A finite `--ticks` run may slightly exceed the requested count when catching up.

```sh
out/runtime/native/ui-host --profile imx6ul-1024x600 \
  --asset-root out/runtime/fixtures --bundle scene.js --pack scene.pak --ticks 6
qemu-arm -cpu cortex-a7 -L /usr/arm-linux-gnueabihf \
  out/runtime/arm/ui-host --profile imx6ul-1024x800 \
  --asset-root out/runtime/fixtures --bundle scene.js --pack scene.pak --ticks 6
```

This diagnostic loads trusted local bundles only. The upstream runtime does not
currently bound guest execution/pending-job drain through its public C interface.
A malicious infinite loop can hang the host; Rust allocation failure aborts.
CI applies process timeouts, not an in-device sandbox. Do not deploy this CLI as
a production daemon or run untrusted bundles. Execution budgets and recovery are
explicit P7 requirements, not silently implemented or certified in P1.

## Evidence

`out/runtime/{native,arm}/build.json` binds commit, project sources, original and
patched source hashes, core/QuickJS artifacts, fixtures and ABI reports.
`test.json` binds actual stdout, intentionally failing output and host CLI runs.
`make verify-runtime` requires both targets from the same clean commit and checks
all recorded hashes before packaging `out/runtime/acceptance.zip`.

Tests cover real node creation/removal/destruction, actual pixel colors and layout,
static damage behavior, Promise ordering, both viewports, wide touch wire encoding
(synthetic, not evdev), pause/resume, catch-up, failure recovery, asset admission
and allocator alignment/reallocation. A wrong expected pixel must return exit 1.
No FPS, RSS, Chinese glyph coverage, board execution or IME claim follows from
these tests. See `board-baseline.md` and `text-input-audit.md` for open admissions.
