# PocketJS on i.MX6UL

Coffee-machine UI port workspace for **1024×600** and **1024×800** Embedded Linux displays.

**Current implementation scope: ARMv7 toolchain validation only.** This is not yet a PocketJS UI host. It does not draw a screen, read touch events, integrate the UI core, or certify compatibility with the deployed coffee-machine BSP.

## Roadmap

[Master roadmap #1](https://github.com/hvritual/ui/issues/1) · [P0 acceptance #2](https://github.com/hvritual/ui/issues/2) · [Stage/dependency index](docs/roadmap.md)

- MVP: P0 toolchain → P1 core/host → P2 display + P3 input → P4 real UI PoC.
- Full Runtime: P5 measured rendering performance → P6 device-service integration.
- Production: P7 lifecycle/release/rollback → P8 hardware reliability and release review.

## Run the P0 gates

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

Run from a **clean, committed checkout**. The GitHub Actions workflow runs these same gates on PRs and main. `make check` only requires Python 3 and Git; cross-build gates require the above native tools. Missing prerequisites fail rather than skip.

`make fetch` downloads only the locked QuickJS source, not the complete PocketJS UI stack. `make build` compiles five real QuickJS C translation units, a small C ABI bridge, and a Rust executable. `make smoke` executes it through `qemu-arm -cpu cortex-a7`, checks ten cases including a 100-runtime lifecycle loop, and confirms a deliberately wrong result exits nonzero.

`out/` contains the ARM ELF, QuickJS archive/license, command logs, ELF dependencies and GLIBC symbol floor, source/tool/package versions, smoke results and SHA256 checksums. The workflow uploads them as an artifact, including failure logs. Check the actual run result and evidence; the presence of this README is not proof of acceptance.

## Important boundaries

Hardware RAM, clock, BSP, kernel, libc, framebuffer details and input protocol remain **unknown** in `targets/imx6ul.json`. Both display dimensions are confirmed user requirements; 30 FPS is a proposed future measurement target. GNU builds may need a newer libc than the device has. QEMU validates basic execution, **not board ABI, display performance or stability**.

Existing device control, network middleware and OTA stay authoritative. No actuator control is exposed by this toolchain smoke.

See [toolchain operation and limitations](docs/toolchain.md) and [third-party sources](docs/third-party.md).
