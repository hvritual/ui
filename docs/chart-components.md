# F7-02B: 1000-point bounded Chart Surface

Parent issue: #88, public SDK #40. This is an embedded, ECharts-inspired data-series **subset**, not a port of Apache ECharts or Qt Quick. Only the functions below are implemented, and not all ECharts options are supported.

## Runtime pipeline

Developer JS `PocketCharts` → F7 `PocketUI` facade → opt-in `ui.draw.chart` native capability → bounded `chart.set` binary-string command → Native ChartModel (at most 2 × 1000 records) → C software rasterizer → PCH1 opaque private bitmap packet → PocketJS Guest resource event → real Image Scene node in existing paint order → existing Presenter/fbdev. The chart counts as **one Scene node**, not one View per point. Only Private Scene Adapter knows the texture handle. No user JS receives a framebuffer pointer, `JSValue`, native handle or writable texture.

## Developer API

Bundle `sdk/ui/js/pocket-ui.js`, then `sdk/ui/js/pocket-charts.js`, then the developer application. `.pui` still includes exactly one `application.js`. The real native loader validates the immutable bundle and explicit capability.

```js
const points=Array.from({length:1000},(_,i)=>({x:i*10,y:(i*37)%10000}));
const plot=PocketCharts.mount(ui,parent,{x:128,y:160,width:768,height:240},{
  type:'line', color:0xff5b843f, data:points,
  xAxis:{min:0,max:10000}, yAxis:{min:0,max:10000}
});
// Call only in a safe application start/action/tick turn:
plot.setOption({type:'scatter',data:points,color:0xff5b843f});
```

Built-in types are `line`, `bar`, `scatter`, `bubble`. Each plot accepts 1..1000 points; data may be `{x,y,size}` objects, with numeric Y arrays accepted for non-bubble series. Current chart canvas is 64..768 pixels wide and 64..320 high; currently up to 2 chart instances per Application. `size` is a 0..255 integer and affects Bubble radius. Axis domains are finite, monotonic numeric intervals of bounded range; out-of-range coordinates reject before native mutation.

Custom bubble-like series reuse one of the existing bounded Native kinds:

```js
PocketCharts.registerSeries('vendor.bubble-health','bubble',items =>
  items.map(p=>({x:p.timestamp,y:p.temperature,size:p.priority})));
const health=PocketCharts.mount(ui,parent,{x:128,y:148,width:600,height:240},{
  type:'vendor.bubble-health',data:items,color:0xff7356a0,
  xAxis:{min:0,max:10000},yAxis:{min:0,max:10000}
});
```

Adapters can transform a developer's business array, but they **cannot** inject a new C renderer mode, unsafe script, arbitrary pixels or an unbounded point set. More Native chart modes require an independently reviewed capability and version.

## Native protocol and bounds

Actual data is strict canonical RFC4648 base64 of exactly 5 octets per point: Little-endian `u16 xNormalized`, `u16 yNormalized`, `u8 size`; both coordinates are normalized to `0..10000`. One 1000-point series = 5000 raw bytes or 6668 base64 characters, below the existing native command TEXT 8192-byte budget. Neither arbitrary JSON nor per-point UI components travel through the VM/native bridge. Every payload is completely decoded and checked before the model revision is changed.

The separate **private PCH1** rendered-bitmap resource contains 32-byte header plus no more than `768×320×4 = 983040` BGRA bytes (less than the pinned PocketJS 2 MiB resource-event maximum). Header includes resource ID, dimensions, revision, length and reserved=0. Renderer validates exact shape and all fields before upload and atomically swaps the texture; failure retains old texture or fails the Runtime rather than displaying a claimed successful update. The Private PSC1 scene wire retains its record format and extends admitted kind to Chart=22; chart data uses no public engine pointer. Out-of-view chart data is marked dirty for next mount and stale chart textures are released at scene removal.

Native raster uses bounded integer plotting, Bresenham segments, bars, dots and filled circles. Image is opaque (light neutral background); alpha compositing within plotted data is not part of this first contract. CPU+RAM for 2×1000 series, update frequency, thermal behavior, touch→visible latency and sustained FPS are **not** certified by QEMU; the P5 #7 full workload must measure both real boards before production admission.

## Pack/run/test

After the repository's exact pinned framework build has prepared licensed assets:

    make -f scripts/chart.mk test-chart-core test-chart-sdk
    make -f scripts/chart.mk test-chart-runtime

Native and ARM source tests actually decode 1000 points for each of 4 modes, verify pixel packet header and snapshot repeat determinism, and reject malformed base64, out-of-bounds, excessive count/size, invalid IDs and tampering. The built-in example `apps/chart-1000/application.js` mounts 1000-point line/scatter and 1000-point bubble in the SAME Application on 1024×600 and 1024×800. Real production Native and ARM ELF run the existing `.pui` with replayed click, record `chart_series=2`, `chart_points=2000`, `nodes<64`, first/last actual PPM hashes differ and byte-identical Native/ARM raster output. The application package uses `ui.core` plus `ui.draw.chart`; previous packages request neither. Unsigned package is development-only and is denied by default.

## Explicit unsupported functionality

No full ECharts JSON grammar, multiple arbitrary series within one chart, stacked plots, logarithmic/categorical/time axis labels, chart legend, tooltips, pinch/zoom/pan, rich paths, anti-aliased lines, animations, gradient/shader, plot clipping beyond canvas bounds, custom native mode injection, HTML/SVG, 3D or chart event picking. Axes currently draw simple lines without text labels. Feature admission must remain exact; shipping a drawing of 1000 points is not a guarantee of responsive 30fps or signed third-party publication.

Production signer, anti-rollback and sandbox remain #9, true dual-board physical HIL/Goodix acceptance remains #49, performance #7. Device control/network/OTA and legacy IME are unchanged.
