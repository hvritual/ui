# Implementation and execution roadmap

This is the dependency/architecture index, not a second live progress database. [Port master #1](https://github.com/hvritual/ui/issues/1), [framework master #32](https://github.com/hvritual/ui/issues/32), child issues and exact-commit PR/Actions evidence retain authority. An issue state is workflow metadata: it does not substitute for software, physical or production evidence.

## Reviewed implementation baseline

Baseline: main `b5881d3e7c54f5e5ba75a49dc079263249d9e830`, tree `eab42e5545398bd88fdcc6995cb0af3e1a3f9959`, 2026-09-30. Later acceptance must name its own commit/tree.

| Scope | Evidence entry | Remaining boundary |
|---|---|---|
| P0 toolchain | #2 / PR #11 | Not a physical-board claim |
| P1–P4 host/display/input/resources | #3–#6; PR #13–#24 and subsequent fixes | Historical ABI and independent board gates remain explicit |
| F1–F6 framework semantics | #34–#39 / PR #43–#48 | Software acceptance, not public SDK or production certification |
| Integrated Coffee app | #49 / PR #50–#55 | Positive Goodix 600 functionality; independent 800/media/fault/human evidence still required |
| Text-session core | #12 / PR #57 | Does not establish rendered language support |
| Visible ASCII touch input | #12 / PR #58 | No real offline Chinese provider, regional layouts or physical keyboard-UI admission |
| Runtime/application separation | #56 | Coffee C still linked into the reference ELF at this baseline |
| Engine decision | #33 / PR #42 | Experimental, not merged/adopted as the mainline engine |

The current integrated path is Coffee reference app → reactive/model/components/navigation/overlay/layout/style/object → interaction → private scene adapter → locked PocketJS/QuickJS → fbdev/evdev. It is not a pure application-package runtime yet.

## User-approved execution order through language admission

The R labels describe execution slices only. Keep ordinary domain names in code and retain original P/F issue numbering. Each slice has an independent PR/evidence record; a missing later gate must not be silently treated as passed.

| Order | Owner | Work and exit | Not included |
|---|---|---|---|
| R0 | #1 / #32 / #49 | Reconcile navigation and actual issue states; distinguish software/physical/production admission | No new hardware approval |
| R1 | #56 | Runtime/application/adapter/target/test ownership manifest and fail-closed build guard | No behavior or renderer changes |
| R2 | #56 | Remove Coffee-specific native logic from the fixed runtime; load two independent, interactive application payloads with the same runtime SHA | No native plugin injection; no dummy second-app proof |
| R3 | #56 with #40 contract hand-off | Minimal `.pui` loading and distinct runtime API, SDK API and package format versions; target/capability/hash/budget admission | Full public SDK/compiler/Studio and production signing are later work |
| R4 | #12 | Real offline Chinese provider/dictionary, bounded async candidate path, existing TextSession/F6 integration, candidate display and single commit | No fixed-candidate stub; no new performance-tuning campaign |
| R5 | #12 | Machine-readable per-locale display/keyboard/editing/IME/shaping/600/800 evidence and accurate pending/unsupported states | Does not claim all markets/languages or missing physical tests are admitted |

R2 owns executable separation; R3 owns compatibility/admission. A minimal internal payload may bootstrap R2; do not create a dependency cycle by requiring the complete F7 SDK before any application can load. Preserve the accepted Coffee, scroll, resource and keyboard workload when proving equivalence.

R4 must not stop at a headless dictionary demo: candidate strings must render, the 600-height field/candidate/keyboard layout must remain usable, stale results must be rejected and production diagnostics must not disclose text. R5 may preserve physical status as pending, but cannot certify an unimplemented software language as supported.

## Remaining route after R5

| Order | Owner | Exit |
|---|---|---|
| R6 | #3–#6 / #49 / #12 | Consolidated, version-bound independent 600/800 functional board and human-review closure |
| R7 | #33 | Same-workload/same-device Engine ADR; adopt, retain current or keep investigation explicitly open |
| R8 | P5 #7 | Measured full Coffee + keyboard + IME performance and technical Go/No-Go |
| R9 | P6 #8 | Versioned intent/state IPC with authoritative existing device services |
| R10 | F7 #40 | Public types/schema/SDK and compatible application package contract; private implementation stays behind the boundary |
| R11 | P7 #9 | Deployment, signing, permissions, interruption recovery and rollback |
| R12 | F8 #41 | Simulator/inspector/replay/certification; software certification must not forge physical PASS |
| R13 | P8 #10 | Independent double-target long-soak/fault and production Go/No-Go |

The user's pause on further early performance tuning remains in effect. Keep #25, engine migration, PxP/DRM, and optional video expansion outside the R0–R5 critical path. Fix functional or safety regressions when necessary, but do not relabel an optimization campaign as a dependency of basic feature development.

## Original P-stage dependencies retained

P0 → P1 → P2/P3 → P4 → P4A → P5 → P6 → P7 → P8. P4 does not depend on P4A; P4A headless work does not depend on P5/P7/P8. Visual input reuses F4 components/overlays and F6 focus/input routing, rather than inventing a second interaction framework. F7 consumes #56 package ownership and the stable framework/input/IPC contracts; P7 consumes version/signing contracts and P8 consumes both deployment and certification evidence.

Two targets need independent hardware records. Do not force RGB565 over an observed 32-bpp mode or infer double buffering from two framebuffer nodes. Preserve guest clock semantics independently of presentation cadence. P5 damage work must measure reduced raster work, not only a clipped final copy.

The 30 FPS / render-work P95 ≤33.3 ms / visible-input P95 <100 ms / provisional UI RSS ≤50 MiB values remain P5 hypotheses until the full workload and actual machine budget are measured. Count total UI+IME resources. Do not sum marginal stage percentiles or report mixed-session presents/time as active-scroll FPS. Pocket Vapor remains outside this route.

## Historical P1/P2 hand-off

`make check-targets`, `make fetch-runtime`, `make test-runtime`, `make test-runtime-arm`, and `make verify-runtime` validate the headless software slice. [board-baseline.md](board-baseline.md) preserves vendor SDK/libc/loader unknowns. Later static-package observations must be recorded as a separate deployment route; they do not prove the old dynamic glibc artifact runs in the reported uClibc environment.

[display.md](display.md) retains Presenter/probe and software framebuffer tests. Read-only probe JSON is memory-layout evidence, not a board photograph or a visible-latency measurement. [framework-board.md](framework-board.md) and #49 contain the later integrated software and positive 600 functional evidence. The remaining 800, media, failure-recovery and human-review items stay independently open until supported.
