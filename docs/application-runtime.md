# Independent applications on a fixed runtime

This is the executable separation slice of #56 (R2). It is not the `.pui`
compatibility/signature gate, public SDK, production sandbox, offline IME,
performance certification or physical board acceptance.

## Runtime and application ownership

The canonical `ui-framework` executable now contains the locked QuickJS and
PocketJS renderer, generic native framework/application bridge, F6 input,
keyboard/text core and Linux display/input backends. Its link-input guard uses
the `runtime` consumer; Coffee C, pager C, reference/test wrappers and application
headers are rejected. Keeping the existing executable name does not change its
ownership contract.

Application state, routes, component composition, themes, paging and local demo
behavior live in `apps/coffee-framework/application.js`. The independent
`apps/control-panel/application.js` provides an interactive counter, settings
page and capturing reset-confirmation modal; it is not Coffee with another title.
Both call the same bounded application contract from #61 and existing native
framework semantics. No third-party native plugins are loaded.

The old Coffee `app.c`/`pager.c` remain only as a frozen test reference. Their
baseline hashes, original framework owner and original test bodies are checked
by `scripts/application_acceptance.py`. They cannot enter a production ELF.

## Internal directory payload (trusted, not a signed package)

An application directory contains `application.js`, `catalog.json`,
`labels.atlas`, built images and notices. Source fonts are not deployed.
`--asset-root` selects this complete application directory without recompiling
or relinking the executable. Existing Coffee commands remain compatible.

The renderer's `scene_guest.js` is embedded as a fixed private native byte array
at build time, separately from all application text/content. The runtime reads
bounded catalog data, percent-encodes it and parses/validates it using a fixed
bootstrap. Catalog bytes are never interpolated as executable source. A legacy
`framework.js` in the application directory is not executed.

The six internal catalog slots retain the current renderer protocol. The
control-panel app uses English in its single selected slot; repeated catalog
entries are not additional admitted product languages.

This directory loader is deliberately for trusted local diagnostic payloads.
R3 must supply package/version/target/capability/hash/budget admission before
external application distribution. Source ownership does not make this public
repository confidential. Private distribution/access control is a separate task.

## Event, resource and privacy constraints

An application has its own QuickJS heap and bounded primitive command bridge.
Application callbacks may update application state but cannot mutate native UI
while F6 dispatch borrows it. Lifecycle callbacks produced during native commands
are queued with component generation and binding revision; stale events are
discarded before the next external input. Native component/list/overlay/focus
ownership remains authoritative. No second interaction framework is introduced.

The native keyboard owns text sessions. Only confirmed nonsensitive fields may
be copied to application state; Password/PIN are never returned by the bridge.
Existing editor screenshot suppression, stale-frame suppression, masked glyphs,
field/overlay generations and cleanup tests remain required. Diagnostics contain
metadata only. Test and review inputs must be synthetic.

## Use the same complete runtime bundle

```sh
# Existing isolated test-board entry, Coffee application directory:
./run-framework.sh imx6ul-1024x600 180

# Desktop/cloud functional invocation of the same executable:
./ui-framework --headless --profile imx6ul-1024x600 \
  --asset-root applications/control-panel --seconds 1 --output NEWDIR
```

Physical use retains the existing display ownership and separate 800-controller
admission requirements. It does not stop device-control/network/OTA services.
No new physical acceptance is implied by the software bundle.

`--replay-input FILE` is a **headless-only** bounded diagnostic. Each line is
`milliseconds phase pointer x y`; phase 0=tick, 1=down, 2=move, 3=up,
4=backend cancellation. Tick rows use zero pointer/coordinates. Up requires an
active pointer; malformed, out-of-order, oversized or unmatched input fails.
The real InputFrame/F6/application/Core path runs, preserving intervening logical
ticks. It does not replace real hardware input evidence or measure visible latency.

## Mandatory R2 evidence

```sh
make check check-targets check-runtime-boundary test-runtime-boundary
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package test-board-framework-verifier
```

These keep the old 42 full-frame outputs and six replay outputs. Native frozen-C
and new application-driven executions must match every byte. New independent-app
cases cover both viewports, state changes, navigation, modal interception,
keyboard privacy, invalid commands/handles and repeated ownership lifecycles.
A deliberately incorrect real-pixel assertion must fail.

Production Native and static ARM executables each run both complete applications
with synthetic input using the **same ELF SHA within that architecture**. The
proof also changes the counter increment in the application payload only and
requires different behavior/pixels without relinking. It rejects malformed
catalog/replay data and physical replay, and demonstrates that injected legacy
`framework.js` is not run. Native and ARM ELF hashes are naturally different.

`application-verification.json` and `static-application-proof.json` bind source,
inputs, output pixels, raw logs and binary hashes. Software reports remain
`physical_hardware=false` and `pui_admission=false`. C/Unicode sanitizer coverage
does not retroactively instrument prebuilt QuickJS/Rust libraries; the separate
#61 VM workflow continues its own fully instrumented QuickJS/program tests.

R2 can be accepted only after exact-head CI and artifact verification. #56 stays
open for R3; #49 and #12 keep their independent hardware/language requirements.
