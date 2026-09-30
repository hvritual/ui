# Pocket Embedded UI on i.MX6UL

Coffee-machine UI workspace for **1024×600** and **1024×800** Embedded Linux displays.

## Implemented baseline and admission boundaries

At the reviewed main commit `b5881d3e7c54f5e5ba75a49dc079263249d9e830` (2026-09-30), this repository contains the real PocketJS/QuickJS Linux host, fbdev/evdev backends, Pocket framework object/layout/style/component/navigation/overlay/reactive/model/interaction layers, and a native Coffee reference application. [PR #57](https://github.com/hvritual/ui/pull/57) adds the Unicode text-session core; [PR #58](https://github.com/hvritual/ui/pull/58) adds visible printable-ASCII, integer, password and PIN touch input.

These are separate claims:

- **Software implementation:** the accepted PRs and their exact-commit tests establish the implemented scope.
- **Physical acceptance:** [#49](https://github.com/hvritual/ui/issues/49) preserves positive Goodix 600 evidence and the remaining independent 600/800, media, fault and human-review gates. A merged PR is not physical acceptance.
- **Production admission:** performance, real device IPC, recovery and long-soak acceptance remain in P5–P8. The reference app only simulates making a drink.

The current `ui-framework` still links Coffee-specific C code. It is **not yet a fixed runtime with independently replaceable applications**; [#56](https://github.com/hvritual/ui/issues/56) owns that separation. Basic ASCII input is not a genuine offline Chinese IME or admission of every Unicode input language. [#12](https://github.com/hvritual/ui/issues/12) remains open. The LVGL spike is separate and is not the mainline renderer.

Start with [the execution/dependency route](docs/roadmap.md), [framework/board operation](docs/framework-board.md), [ASCII keyboard scope](docs/ascii-keyboard.md), and [text-session semantics](docs/text-session.md). Issue checkboxes, software evidence, physical acceptance and production admission must not be conflated.

## Build foundations

The cloud development baseline is Ubuntu 22.04 x86_64, GNU ARM hard-float GCC 11, Rust 1.90.0 and QEMU user mode. It is not the device root filesystem. Run from a **clean, committed checkout**; missing prerequisites fail rather than skip.

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends make git python3 patch gcc-arm-linux-gnueabihf libc6-dev-armhf-cross binutils-arm-linux-gnueabihf qemu-user
rustup toolchain install 1.90.0 --profile minimal --component rustfmt --target armv7-unknown-linux-gnueabihf
make check
make fetch
make build
make smoke
make verify
```

P0 compiles the locked QuickJS C source, a C ABI bridge and a Rust smoke executable. Its tests do not establish UI or physical-board performance.

P1 uses the separately pinned UI C ABI workspace:

```sh
rustup toolchain install nightly-2026-07-02 --profile minimal --component rust-src --target armv7-unknown-linux-gnueabihf
make check-targets
make fetch-runtime
make test-runtime
make test-runtime-arm
make verify-runtime
```

See [Linux host gates](docs/linux-host.md), [board observations](docs/board-baseline.md), [source/license records](docs/third-party.md) and [P0 operation](docs/toolchain.md). The single-owner host preserves guest/jobs/core timing while rendering on separate opportunities. Tests execute actual layout, DrawList and pixels, not a replacement renderer.

## Display and complete framework gates

```sh
make test-display-unit
make test-display
make test-display-arm
make verify-display
```

The Presenter uses queried stride, offsets and bitfields without forcing a new display mode. It supports admitted 32-bit layouts and exact RGB565 conversion. Pan/VSync, tearing and physical geometry require their own device evidence. See [display.md](docs/display.md).

The complete framework has additional resource/build prerequisites; follow [framework-board.md](docs/framework-board.md), rather than interpreting the following gate names as a complete clean-machine setup:

```sh
make test-keyboard-layouts
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package test-board-framework-verifier
```

The matching executable, private scene guest adapter and catalog must ship together until #56 establishes independent application loading. Board packages and tests must identify their actual build commit, not merely the later merge commit.

## Device and safety boundaries

The historical 1024×600 observation in `targets/boards/myimx6ek140-1024x600.json` records Linux 4.9.88, Buildroot 2019.05-rc1, Linux-visible RAM 242976 KiB and 32-bpp geometry. Newer Goodix evidence and static test packages do not retroactively certify the old glibc dynamic binary or a reproducible vendor uClibc SDK. The 1024×800 profile does not inherit the 600 controller, orientation, ABI or performance.

Never upgrade device libc to accommodate a cloud build. Read the actual ELF's INTERP/NEEDED and ABI evidence; a static package running on a tested board is not blanket BSP compatibility. QEMU is functional evidence, not physical display, touch, latency, memory-budget or long-soak acceptance.

Existing device control, network middleware and OTA remain authoritative. UI sends intent only when the future P6 contract admits it; it does not access actuators. The diagnostic/reference runtime is not yet a certified production daemon or untrusted-application sandbox. Use synthetic input in review evidence; never collect real credentials or rewrite automatic `visual_validated=false` flags.
