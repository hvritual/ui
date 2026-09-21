# Implementation roadmap

Authoritative task status and acceptance evidence live in [master issue #1](https://github.com/hvritual/ui/issues/1) and its child issues. This file is a navigation/architecture index, not a duplicate progress database.

| Layer | Stage | Issue | Dependencies |
|---|---|---|---|
| MVP | P0 Rust + QuickJS ARMv7 toolchain | [#2](https://github.com/hvritual/ui/issues/2) | None |
| MVP | P1 Core, Linux host contracts, board baseline | [#3](https://github.com/hvritual/ui/issues/3) | P0 |
| MVP | P2 framebuffer display | [#4](https://github.com/hvritual/ui/issues/4) | P1 |
| MVP | P3 evdev touch/key input | [#5](https://github.com/hvritual/ui/issues/5) | P1; E2E requires P2 |
| MVP | P4 multilingual fonts, assets, representative UI | [#6](https://github.com/hvritual/ui/issues/6) | P1–P3 |
| MVP | P4A text sessions, keyboard and offline IME | [#12](https://github.com/hvritual/ui/issues/12) | P1/P3/P4; P2 for display |
| Full Runtime | P5 rendering and IME performance | [#7](https://github.com/hvritual/ui/issues/7) | P4 and P4A frozen workloads |
| Full Runtime | P6 existing device-service IPC/text fields | [#8](https://github.com/hvritual/ui/issues/8) | P1/P4; text integration requires P4A; exit requires P5 |
| Production | P7 lifecycle, language packs, release/rollback | [#9](https://github.com/hvritual/ui/issues/9) | P5/P6 |
| Production | P8 reliability and production review | [#10](https://github.com/hvritual/ui/issues/10) | P5–P7 |

Each issue defines inputs, outputs, exclusions, allowed/prohibited changes, acceptance commands and risks. P0 commands and P1 headless gates are implemented. Commands in later issues are future deliverables, not existing features. P4A does not depend on P5/P7/P8 acceptance: those stages consume its outputs without a dependency cycle.

## Architecture boundary

Implemented P1 path: bounded native HostOps fixture → QuickJS guest → PocketJS C runtime/core → layout/DrawList → software offscreen framebuffer. Component-compiler integration, physical display/input and IME adapters remain separate work. P1 does not create a replacement core or register an official upstream platform.

Both display sizes require independent hardware evidence. The provided 1024×600 board reports 32 bpp; RGB565 must not be forced over its real pixel format. P2 must query stride, offsets, node roles and synchronization. Damage tracking must reduce raster work, not just final copies. Guest clock semantics remain intact while rendering cadence is separate. PxP/DRM are optional measured optimizations, not baseline dependencies.

The 30 FPS / ≤33.3 ms render P95 / <100 ms visible-input P95 / provisional ≤50 MiB UI RSS targets are hypotheses for P5, not measured P0/P1 performance. Include total IME resources and representative machine workload when freezing budgets. Pocket Vapor is outside this roadmap.

## P1 hand-off

`make check-targets`, `make fetch-runtime`, `make test-runtime`, `make test-runtime-arm` and `make verify-runtime` check the headless software slice. Vendor SDK/libc/loader admission remains separate and unresolved. Only the 1024×600 board has user-provided measurements. See `board-baseline.md` for exact observations and unknown fields.

P1's source audit identifies reusable upstream IME and keyboard helpers, not a completed Linux input method. P4A must provide the local engine/dictionary, editor sessions and keyboard UI. P5/P6/P7/P8 retain the IME performance, field, language-pack and reliability requirements in their issues.
