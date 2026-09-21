# Implementation roadmap

The authoritative task status and acceptance evidence live in [master issue #1](https://github.com/hvritual/ui/issues/1) and its child issues. This file is a stable navigation/architecture index, not a duplicate progress database.

| Layer | Stage | Issue | Dependencies |
|---|---|---|---|
| MVP | P0 Rust + QuickJS ARMv7 toolchain | [#2](https://github.com/hvritual/ui/issues/2) | None |
| MVP | P1 Core, Linux host contracts, board baseline | [#3](https://github.com/hvritual/ui/issues/3) | P0 |
| MVP | P2 framebuffer display | [#4](https://github.com/hvritual/ui/issues/4) | P1 |
| MVP | P3 evdev touch/key input | [#5](https://github.com/hvritual/ui/issues/5) | P1; E2E requires P2 |
| MVP | P4 Chinese fonts, assets, representative UI | [#6](https://github.com/hvritual/ui/issues/6) | P1–P3 |
| Full Runtime | P5 rendering/performance validation | [#7](https://github.com/hvritual/ui/issues/7) | P4 |
| Full Runtime | P6 existing device-service IPC | [#8](https://github.com/hvritual/ui/issues/8) | P1/P4; exit requires P5 |
| Production | P7 lifecycle, release and rollback | [#9](https://github.com/hvritual/ui/issues/9) | P5/P6 |
| Production | P8 reliability and production review | [#10](https://github.com/hvritual/ui/issues/10) | P5–P7 |

Each issue defines inputs, outputs, exclusions, allowed/prohibited changes, acceptance commands, risks and whether one cloud task can complete it. Commands listed in P1–P8 are future deliverables, not implemented P0 features.

## Architecture boundary

Future path: component bundle → QuickJS guest → PocketJS core/C ABI → software renderer → Linux display/input host. P0 only proves the Rust/C/QuickJS toolchain. It does not create a parallel replacement UI core.

The display size is either 1024×600 or 1024×800. Each hardware variant needs independent evidence. RGB565 is a preferred candidate, not a forced assumption about the framebuffer. Damage tracking must reduce raster work, not just final copies. Guest/frame-clock semantics must remain intact even when presentation is paced at 30 FPS. PxP/DRM are optional measured optimizations, not baseline dependencies.

The 30 FPS / ≤33.3 ms render P95 / <100 ms visible-input P95 / ≤50 MiB provisional RSS targets are hypotheses for P5, subject to the actual board budget. No performance numbers have been measured by P0. Pocket Vapor is outside this roadmap.
