# PocketJS on i.MX6UL

Coffee-machine UI port workspace for **1024×600** and **1024×800** Embedded Linux displays.

The repository contains an independent **P0 ARMv7 toolchain smoke** and a **P1 real PocketJS Linux headless host**. P1 executes QuickJS, the upstream C runtime, UI core/layout/DrawList and software rendering into memory. P2 adds an explicit fbdev Presenter/probe/display-test path; physical panels remain untested. Input devices, IME and compatibility with the deployed coffee-machine BSP are not certified. Actual acceptance evidence lives in the issues and Actions results, not in this README.

## Roadmap

[Master roadmap #1](https://github.com/hvritual/ui/issues/1) · [P0 #2](https://github.com/hvritual/ui/issues/2) · [P1 #3](https://github.com/hvritual/ui/issues/3) · [P2 #4](https://github.com/hvritual/ui/issues/4) · [Stage/dependency index](docs/roadmap.md)

- MVP: P0 toolchain → P1 core/host → P2 display + P3 input → P4 multilingual resources/demo → P4A text editing/keyboard/offline IME.
- Full Runtime: P5 measured rendering and IME performance → P6 device-service integration.
- Production: P7 lifecycle/language-pack release/rollback → P8 hardware reliability and release review.

## P0 gates

The supported **cloud development baseline** is Ubuntu 22.04 x86_64, GNU ARM hard-float GCC 11, Rust 1.90.0 and QEMU user mode. It is not the device root filesystem.

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends make git python3 gcc-arm-linux-gnueabihf libc6-dev-armhf-cross binutils-arm-linux-gnueabihf qemu-user
rustup toolchain install 1.90.0 --profile minimal --component rustfmt --target armv7-unknown-linux-gnueabihf
make check
make fetch
make build
make smoke
make verify
```

Run from a **clean, committed checkout**. `make fetch` downloads the locked QuickJS source for P0. `make build` compiles five real QuickJS C files, a C ABI bridge and a Rust executable. `make smoke` runs ten real ARM/QEMU cases and checks an intentional failure. Missing prerequisites fail rather than skip.

## P1 Linux Host

See [build and acceptance](docs/linux-host.md), [observed board baseline](docs/board-baseline.md), and [text/IME capability audit](docs/text-input-audit.md). P1 uses the upstream-pinned `nightly-2026-07-02` standalone UI C ABI workspace, separately from P0's Rust compiler.

```sh
sudo apt-get install -y patch
rustup toolchain install nightly-2026-07-02 --profile minimal --component rust-src --target armv7-unknown-linux-gnueabihf
make check-targets
make fetch-runtime
make test-runtime
make test-runtime-arm
make verify-runtime
```

The host uses bounded trusted local assets, a single UI owner, monotonic time, 60 guest/core turns and one offscreen render per two turns. Tests validate actual pixels/layout, Promise ordering, pause/resume, errors and repeated cleanup. The test fixture uses native HostOps, not yet the component compiler. Output and evidence are in `out/runtime/`; an Actions artifact preserves successful results and failure logs.

## P2 framebuffer Presenter

See [display contracts, gates and safe device operation](docs/display.md). The Presenter accepts validated 32-bit RGB888 bitfields or exact RGB565, using queried stride, offsets and mapping limits without changing display modes. Read-only probe and deliberate display-test are separate commands. The core-to-presenter tests use real PocketJS rendering and explicitly synthetic fbdev syscalls; independent full-frame goldens cover both viewports, text, images, clipping and animation.

```sh
make test-display-unit
make test-display
make test-display-arm
make verify-runtime
make verify-display
```

Display evidence is in `out/display/`. Pan remains disabled, vsync is not probed, and row copying may tear. **Device commands require prior BSP/ABI admission and exclusive display ownership; current cloud binaries are not a production install.**

## Important boundaries

User-provided measurements for the **1024×600** board are in `targets/boards/myimx6ek140-1024x600.json`: Linux 4.9.88, Buildroot 2019.05-rc1, Linux-visible RAM 242976 KiB, and 32-bpp display geometry. These observations do not establish libc/loader/SDK compatibility. The 1024×800 profile has no physical-board record. P0's historical `targets/imx6ul.json` remains unchanged; P1 profiles and observation records are separate.

Read each binary's actual GLIBC requirements. Never upgrade device libc to accommodate a cloud baseline. QEMU execution is **not board ABI, screen performance, RSS or stability evidence**. Existing device control, network middleware and OTA stay authoritative; the UI does not access actuators. The headless CLI is a diagnostic for trusted bundles, not a production daemon or sandbox.

See [P0 operation](docs/toolchain.md) and [third-party sources](docs/third-party.md).
