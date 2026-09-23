# Coffee Demo / P4

This is a real PocketJS reference app on the existing Linux Host, not a browser screenshot. It never controls the heater, pump, payment or dispense service. Eight drinks, two pages, a confirmation modal, simulated progress and six static-label locales establish a frozen P5 workload. The supplied cup silhouettes are diagnostic illustrations, not product photography approved by the manufacturer.

## Device run

Use the 1024×600 machine already admitted by P3. While idle, pause the existing UI/input consumer using its normal maintenance process. Keep device control, network and OTA running. This program does not stop them for you.

```
tar xzf imx6ul-coffee-demo.tar.gz
cd coffee-demo
sha256sum -c SHA256SUMS
./run-coffee-demo.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /tmp/coffee-media 3600
```

The session is bounded to 3600 logical turns, approximately one minute plus any rendering delays. It writes fb0 but does not change display mode, enable VSync or PAN, or restore the previous image after exit. `logs/coffee-*` includes startup, JSON, input/guest/CPU-present CSV and checksums. This is not a production service launcher.

## Replace images while the demo is running

Use a second terminal. The UI polls local store metadata every 30 guest turns, but applies a complete image generation only on the home page with no active gesture. Modal/preparation/active-touch defer activation. UI state is retained; no executable rebuild, UI restart or machine reboot is needed. After application, `MEDIA_APPLIED generation=...` appears in the startup log and the resource revision marker changes.

The `updates/demo-a.zip` and `demo-b.zip` are signed test resources. `updates/demo.public` is their ephemeral public verification key; the private signing seed is not shipped. Try:

```
./mediactl install /tmp/coffee-media updates/demo.public updates/demo-a.zip
./mediactl install /tmp/coffee-media updates/demo.public updates/demo-b.zip
./mediactl status /tmp/coffee-media
./mediactl rollback /tmp/coffee-media updates/demo.public
```

`MEDIA_INSTALLED` means the requested local generation is committed; it is **not** a UI-applied acknowledgment. The UI can be busy or not running. Confirm `MEDIA_APPLIED` and the screen. The same current generation is used after app restart. `/tmp` itself is volatile: choose an approved persistent local filesystem for deployment; that device-specific path has not been assumed.

### USB

Copy the signed ZIP to an already mounted USB filesystem and invoke the same installer with its actual path. The tool does not guess a mountpoint or mount devices. It copies verified resources into the local store; USB can be removed after the installer succeeds. It never executes files from USB or treats autorun scripts as updates.

```
./mediactl install /tmp/coffee-media updates/demo.public /actual/usb/path/demo-b.zip
```

### HTTPS

Publish the same signed bundle to your own HTTPS endpoint, then use:

```
./mediactl fetch /tmp/coffee-media updates/demo.public https://your-asset-host.example/demo-b.zip
```

This is a URL template, not a configured service. Certificate verification stays enabled. The board needs a valid clock and CA trust store; neither is inferred from the P3 display test. No redirects, URL credentials or HTTP downgrade are allowed. Download is bounded to 16 MiB and 90 seconds; incomplete/invalid downloads do not replace the current generation. TLS tests use a local certificate-verified HTTPS fixture, not an already validated production endpoint.

## Your own photographs

Use the desktop `mediactl` binary (Windows provided) or build `tools/media`. Prepare eight files, one per logical ID: `espresso`, `americano`, `latte`, `cappuccino`, `flatwhite`, `mocha`, `tea`, `water`, with `.png` or `.jpg`. To change one image, keep the other seven in the next complete bundle. Raw images are not compiled into JS. The MVP uses whole-generation updates, not partial/delta packages.

```
mediactl keygen operator
mediactl pack images campaign-001 operator.private campaign-001.zip
```

Keep `operator.private` on the signing workstation, never on the coffee machine, USB delivery media or public server. Provision `operator.public` on the device through an authorized channel and use it instead of `demo.public`. **Do not trust a public key included in an incoming bundle.** Images/authorizations are operator-supplied; the packaging command does not confer copyright permission.

## Data and safety boundary

The signed manifest permits exactly eight image IDs and canonical PNG/JPEG paths, hashes, a version and license declarations. Unknown fields, duplicate keys/entries, missing images, unreferenced files, traversal, symbolic links, wrong signature/key, hash mismatch, unsupported codec and excessive size are rejected. External packages cannot replace JS, native code, font atlases, UI locale definitions or control parameters.

Each image is <=2 MiB encoded, <=2048 pixels on either axis and <=1,048,576 decoded pixels. The installer decodes outside the UI and aspect-fits to 256×144 straight RGBA. Nearest-neighbor is the MVP resampler; prepare images at the final dimensions for best quality. Eight textures use 1,179,648 pixel bytes, with an additional bounded generation during replacement. This is a pixel budget, not measured whole-process RSS. WebP/SVG/GIF/video are not supported.

Store directory and parents must be trusted local administration paths, not writable by untrusted users or USB-backed. A single Linux writer lock protects current+previous generations. Staged files are synced before atomic rename of the current pointer; readers use complete immutable generation files. A failed validation/download leaves the old generation. An OS/storage error **after** pointer rename may mean commit status is uncertain: inspect `mediactl status` and logs instead of claiming failure guarantees no change. Real power-loss/fsync durability on the board's filesystem is still P7/P8 work. On crash, unreferenced temporary files may need maintenance. CRC on the UI packet detects corruption, not publisher identity; signature verification is the installer's responsibility and the local store is a trusted boundary.

The application refuses a corrupt new packet and retains current textures. Missing store falls back to built-in images. A rejected generation is not re-tried each frame; install a corrected generation or restart after authorized store repair. Runtime OOM/process crash and persistent-store hardware corruption are not claimed to be production-qualified.

## Text capability matrix

| Scope | P4 admission |
| --- | --- |
| zh-CN / en-US / de-DE / fr-FR / es-ES / pt-PT | Fixed NFC application labels in the bundled subset atlas, tested at both viewports |
| Chinese arbitrary input, traditional Chinese, Japanese, Korean | Not admitted by this subset |
| Arabic / RTL / complex shaping / combining-sequence editing | Not admitted; never reverse strings to simulate bidi |
| UI locale / input locale / keyboard layout | Separate; input locale and keyboard layout remain unset |
| IME, preedit, caret, candidate selection | P4A #12, not implemented here |

The font source is pinned Noto Sans CJK SC Sans2.004 with its OFL notice. Build checks every label codepoint and rendered advance. The target loads only a bounded bitmap subset, not a full font. Upstream atlas text measurement walks codepoints; glyph presence is not proof of shaping or bidi. The static-label atlas must not be marketed as full international text entry.

## Verification and remaining physical gates

```
make build-demo
make test-demo-native
make test-demo-arm
make test-assets
make verify-demo
make package-demo
```

Tests use real QuickJS/Core: locale pages, select/modal/cancel, swipe, simulated progress/done, cancellation, data-only resource delivery, busy deferral, malformed packet rejection and repeated texture replacement. Native/ARM full-frame outputs are compared. Image signing/install/rollback and HTTPS rejection have independent Go tests. The original P0/P1/P2/P3 gates remain required.

1024×800 is a separate software viewport; its hardware is still unadmitted. P4 physical UI appearance, USB copy/removal, actual board HTTPS trust/network and dynamic switching need the resulting device logs and visual confirmation. P4 does not certify 30 FPS, memory targets, power-cut safety or production release readiness. Image/font/scene hashes are frozen for later P5 tests; changing an image generation creates a different benchmark identity.
