# F7-02B development: custom 1024-point trend chart

Implementation is prepared on `feat/f7-02b-thousand-point-charts`, following the
separate F7-02A slice in PR #90. **Cloud compilation, real runtime integration,
artifact delivery and physical verification are not yet accepted.**

The requested build environment is ChatGPT cloud; local work is source development
and verification using cloud-built artifacts. GitHub Actions is not a replacement
for that required build provenance. The existing ChatGPT cloud conversation failed
with `systemError`; creating its requested replacement returned
`ChatGPT Work Cloud is not available for this account`. No local native build or
new physical application run has been used to bypass this requirement.

## Developer experience

`apps/trend-chart/charts.js` is an independent JS module with a small ECharts-like
API, not the ECharts package. It registers a factory through PocketCustom and uses
only the public facade. Example:

```js
const registry = PocketCustom.install(ui);
PocketCharts.register(registry);
const chart = PocketCharts.init(registry, page,
  {x:64,y:160,width:896,height:240});
chart.setOption({
  series:[{type:"line",data:Array.from({length:1024},(_,i)=>i%100)}],
  yAxis:{min:0,max:100},
  dataZoom:{start:0,end:100}
});
```

`setOption` updates data and the visible x domain; `resize(rect,option)` updates
geometry and data while retaining component handles. The app demonstrates 1024
integer simulated values, all 1024 vertices and 1023 segments, followed by a data
and layout update. Values are not binned into the old 32 rectangle budget. A chart
uses one background, one polyline View and two axis Views; the whole demo has
eight semantic nodes including its catalog text labels. Both 600/800 profiles
remain explicit. The public module clamps out-of-axis values, rejects invalid or
non-finite data and validates 2–4096 samples and the zoom domain.

## Protocol and ownership

The new optional primitive is `{kind:"polyline",points:[{x,y},...],styleRef}`.
Each custom instance has at most 4096 polyline points across its primitives. A
native application owns at most four drawing surfaces, each at most 262144 pixels,
width <=1024 and height <=800. Coordinates are integer local pixels inside that
surface. Stroke width is one pixel; arbitrary Path, Arc, shader and arbitrary
native extension support are not claimed. Stroke color resolves an existing
theme background token through the supplied style ref.

The facade sends versioned `draw.begin`, sequential fixed-size `draw.points`
chunks and `draw.commit`. Every chunk is checked natively. Until a complete commit,
the previously committed drawing remains intact. Unknown version, count overflow,
coordinate overflow, incomplete/duplicate chunks, stale/wrong owner or mutation
inside borrowed event dispatch is rejected by the native policy. UI tree ownership
and generation checks are authoritative; page teardown reclaims those slots.
Drawing commands use the existing `ui.core` grant and do not confer image-file,
device, IPC or arbitrary resource-upload authority.

The engine accepts a separate `POCKET_DRAW_RESOURCE` v1 containing copied fixed
capacity polyline records, rasterizes the actual segments with integer Bresenham,
then transfers a bounded PDW1 RGBA texture packet to the private adapter. The real
pinned Pocket Core draws that texture inside the owner View's normal clipping and
paint order. This adds a production drawing implementation; there is no fake
renderer and no hundreds-of-nodes line approximation. PSC1 bytes and upstream
Core source revisions remain unchanged. Updates invalidate the adapter's texture
binding even when the scene geometry is unchanged. Owner deletion removes its
private texture. Trusted development JS remains the trust boundary, with the
existing loader, heap, command/fuel and fixed diagnostic-log policies preserved.

## Cloud build and admission checklist

From the exact committed checkout, in ChatGPT's Linux build environment:

1. Read AGENTS.md, the cloud runtime skill and the existing Coffee workflow.
2. Run its pinned prerequisite, original renderer, asset and full Framework gates.
3. Run `make -f scripts/public-sdk.mk test-public-sdk`, including negative SDK
   cases and the real native C raster unit.
4. Run `make -f scripts/public-sdk.mk test-public-sdk-runtime`. It executes the
   counter, bar chart and trend chart on the same production Native and ARM
   executable hashes. Trend admission requires two uploads of exactly 1024
   points, eight semantic nodes, actual first/last snapshots containing every
   independently expected vertex and axes, both target dimensions and Native/ARM
   byte equality. Default unsigned rejection is exercised separately.
5. Build the original static ARM board executable with `make build-board-framework`.
   Preserve ABI/readelf, source commit/tree, raw logs and test outputs, and hashes
   of SDK/module/test sources, ELF, `.pui`, assets and screenshots.
6. Return downloadable cloud artifacts. Verify hashes locally and stage under a
   unique `/opt/ui-framework` test directory, preserving the existing launcher,
   running UI, control/network/OTA services and recovery path. Inspect current
   display consumers before launching. Use the observed 1024x600 profile on the
   connected board; 800 software results do not establish an 800 physical board.
7. Run the static production ELF with the new `.pui`, explicit unsigned development
   opt-in and physical mode. Collect its report, output pixels, CPU/RSS and logs.
   Never count Node mocks or C unit tests as this physical evidence.

Acceptance remains blocked until the specified cloud environment is available,
the source compiles and these actual artifact/runtime checks pass. No completion,
merge, general performance admission, #49 HIL closure or production authentication
is implied by the development tests.
