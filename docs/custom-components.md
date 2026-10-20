# F7-02A: reusable components and Surface v0

Scope: [#88](https://github.com/hvritual/ui/issues/88), built on F7-01. This experimental, trusted-development JS module reuses the existing public PocketUI facade and `.pui` v1 loader. Native code, renderer, scene wire, capabilities and existing application assets are unchanged. Implementation and tests do not establish physical acceptance or production admission.

## Registration and ownership

Load JavaScript in this order: `pocket-ui.js`, `pocket-custom.js`, reusable module(s), application entry. The builder concatenates these into the single `application.js` member; no JS module loader, TypeScript compiler or native plugin is added. Declaration-only types live in `sdk/ui/types/custom.d.ts`.

In `PocketUI.define().start(ui, target)`, call `PocketCustom.install(ui)` once, register named factories, then mount under an existing page-owned parent:

```js
const components = PocketCustom.install(ui);
components.register("vendor.status.block", (size, props) => [
  {kind:"rectangle", x:0, y:0, width:size.width,
   height:Math.round(size.height * props.percent / 100), styleRef:4}
]);
const block = components.mount("vendor.status.block", page,
  {x:64, y:160, width:320, height:200}, {percent:25}, 2);
// Later in start/action/tick, while the owning page is alive:
block.update({percent:80}, {x:64, y:160, width:400, height:240});
```

Factories are synchronous functions of frozen `{width,height}` and application props, returning a dense array of primitives. The application/module validates domain values; Surface validates all emitted geometry. Factories receive no UI context or native handles. Namespace names use lowercase segments separated by dots or hyphens, at most 96 characters, with at least two segments. There are at most 32 registered factories per context. Unknown names, duplicate registration/install and reentrant registry/instance use fail before host mutation.

Each mounted instance owns one background View and 1–32 primitive Views. Updates preserve the count, ordered primitive kinds, style references and all handles; geometry alone changes. The returned frozen instance exposes `primitiveCount` and `update(props, rect?)`, with no handle, arbitrary command dispatcher or destroy method. Multiple mounts are independent. Invalid updates are rejected before host mutation and can be corrected. Native failures can leave partial changes because the current API has no rollback: that instance permanently rejects further updates and the application must propagate the error.

Page/navigation teardown owns native cleanup. Drop the JS instance when its parent page is removed; do not update it afterward. If attempted, native stale-handle rejection remains authoritative. This module does not claim independent unmount, resource disposal, asynchronous callbacks or a malicious-code sandbox. Retaining a page's JS instance after teardown does not keep its native objects alive.

## Geometry and style contract

- Surface `{x,y,width,height}` uses integer local coordinates: x/y 0–65536, width/height 1–65536. Parent clipping and native layout policy remain authoritative.
- Rectangle has exactly `{kind:"rectangle",x,y,width,height,styleRef}`. All geometry is nonnegative integral, fully inside the declared Surface. Zero width/height is allowed to retain a hidden primitive without rebuilding.
- Axis line has exactly `{kind:"line",x1,y1,x2,y2,thickness,styleRef}`. Exactly one axis must be constant. Endpoints are half-open; positive-side thickness must also fit inside the Surface. Zero-length and diagonal lines are rejected.
- Existing style refs 1–65535 select theme tokens through PocketUI's style rules. No arbitrary color or text fields are accepted. Text remains the separate public catalog-based API.
- At most 32 primitives on every mount and update. No Path, Polyline, Arc, Canvas, shader, framebuffer, `lv_*`, JSValue, arbitrary C extension or hundreds of nodes approximating curves.

The caller must use start/action/tick for mutations, following the public SDK owner-turn rules. No new event dispatcher or permissions are introduced. Installation does not grant device, IPC, image or input access. Global native policy is the authority for capabilities and managed heap. The source-boundary checker catches accidental private API use; trusted JavaScript can still access globals and is not isolated by this API.

## Independent example and gates

`apps/custom-chart/bar-chart.js` registers `example.chart.bars`; `application.js` uses only PocketUI, PocketCustom and that module. Six offline simulated values change on tick 8 while the Surface moves and resizes. Eight primitive handles plus the background are reused on both 1024×600 and 1024×800 targets. The reusable bar module accepts up to 30 values (plus two axis lines), rejecting NaN, infinities, invalid values and longer sequences.

Run from the canonical Linux build environment with Python 3 and Node:

```sh
make -f scripts/public-sdk.mk test-public-sdk
```

After the original Framework resources and production Native/ARM ELF binaries are built, run:

```sh
make -f scripts/public-sdk.mk test-public-sdk-runtime
```

This first runs the existing counter, then packages the custom chart with the same catalog/glyph/image resources and executes it on the **same binaries**. Custom integration requires the counter evidence's source commit and ELF hashes to match, and verifies that neither executable changes during the runs. The packaged code, SDK/type/module sources, tests, raw runtime logs, reports and first/last PPM snapshots receive independent SHA256 evidence. All six bar heights, empty space, axis lines, moved background and native/ARM snapshot equality are checked. Unsigned packages are tested for default native rejection, and positive runs explicitly use `--allow-unsigned-package`.

To package only:

```sh
python3 scripts/public_sdk.py pack --example custom-chart \
  --assets out/framework/applications/control-panel --output out/public-sdk/custom-chart.pui
```

The contract CI records source and unit-test hashes. The existing Coffee workflow runs the production ELF tests on the exact PR head and uploads `out/public-sdk/` evidence. Unit fixtures, Node bridges and synthetic pixel-checker tests are not production rendering evidence. Missing Linux file primitives, licensed resources, production binaries or baseline evidence fail the gates; Windows without Linux/WSL cannot satisfy the full codec/runtime gates.

General curve/path/arc support requires F7-02B's versioned, fixed-capacity native drawing protocol in a separate issue/PR. The current contribution does not advance #49's real i.MX6UL 600/800 HIL, #7 performance or #9 signing/release admission.
