# ARMv7 toolchain operation

## What is implemented

`toolchains/sources.lock.json` pins PocketJS as a **reference**, and pins the actual QuickJS C source used by the smoke build. The QuickJS pin matches the native C-source strategy in PocketJS `tools/3ds-toolchain.ts` at the recorded revision; i.MX6UL compilation uses GNU/Linux Cortex-A7 flags, not 3DS flags.

The build uses GCC 11, `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`, Rust 1.90.0 and `armv7-unknown-linux-gnueabihf`. `native/quickjs-smoke/bridge.c` contains all `JSValue` objects so Rust does not assume 32-/64-bit QuickJS value layout. The bridge is test scaffolding, not an application API or the PocketJS C runtime. P1 integrates the actual `engine/ui-cabi` and `engine/quickjs-c/pocket_runtime.c`.

No downloaded source is patched. A cache with a wrong commit, changed tracked/untracked source or wrong VERSION fails. Remove an invalid cache explicitly only after inspecting why it changed; the tool never repairs it silently.

## Acceptance boundaries

1. `make check`: configuration, synthetic ELF/parser gate fixtures, negative results and a real temporary Git-tamper test. These are checker tests, not runtime evidence.
2. `make fetch`: fetch and verify the exact QuickJS commit/VERSION.
3. `make build`: compile QuickJS + C bridge + Rust; require clean project state; inspect actual ELF32 ARM EABI5 hard-float header and ARM loader; record GLIBC version needs.
4. `make smoke`: use Cortex-A7 QEMU user mode and the recorded GNU sysroot. Arithmetic, C callbacks, Chinese JSON, typed arrays, Promise jobs, exception, syntax, interruption, memory limit and 100 runtime create/free cycles are verified. A deliberate 41-vs-42 mismatch must return nonzero.
5. `make verify`: validate commit, tracked-source/lock/config/binary hashes, smoke stdout, exit status and the negative test; emit the final manifest and checksums. Previous PASS markers are invalidated at the start of a new attempt.

`out/build.json` binds source hashes and installed tool/package versions. `out/smoke.json` binds execution to that build and binary. `out/verification.json` and `out/SHA256SUMS` summarize the successful attempt. This prevents accidental stale/mixed evidence; it is not a cryptographic proof against a malicious author who can replace both checker and evidence. Independent GitHub execution logs and review remain necessary.

## Reproducibility limits

Source revisions, Rust version, action revisions, architecture flags and GCC major are pinned. The Ubuntu runner image and apt package patch revisions are **recorded, not hermetically pinned**. This is a repeatable, traceable build recipe, not a bit-for-bit reproducible SDK image. P7 can promote a reviewed SDK/container digest after the real BSP is known. P0 does not claim musl, vendor Yocto SDK or Buildroot support.

The cross GCC's built-in system paths are used. `/usr/arm-linux-gnueabihf` is QEMU's library prefix, not blindly injected as a GCC `--sysroot` (Debian/Ubuntu cross GCC already configures its search paths). Do not copy this generic GNU binary onto production hardware until its loader, GLIBC symbols, CPU features and kernel compatibility are checked against the board.

QEMU emulates user instructions and syscalls on the cloud host. It is not an i.MX6UL LCD/input/BSP emulator and provides no trustworthy board FPS/RSS/startup estimate. 100 create/free cycles are a cleanup smoke test, not a leakage or long-soak certification.

## Hardware data required for P1/P2

For each display variant, collect SoC/CPU clock/RAM, SDK identity and sysroot, kernel/libc versions, framebuffer format/stride/virtual size/offset/refresh and evdev capability/ABS/MT protocol. Keep missing fields null. Avoid changing production services merely to gather data. P1/P2 define the read-only probes and real-board admission gates.

## Troubleshooting

`missing required executable`: install the documented baseline tools; do not skip the failing gate. `wrong GCC major`: use Ubuntu 22.04 GCC 11 or explicitly design a separate SDK profile. `cached sources are modified`: inspect and replace the cache with a clean pinned checkout. `stale commit evidence`: rebuild and rerun smoke after any source change. `GLIBC_x.y not found` on a board: this is an ABI mismatch, not a display issue; rebuild using the correct reviewed BSP toolchain. `PORT_FAILED` or `SMOKE_FAILED` must never be converted to success in a wrapper.
