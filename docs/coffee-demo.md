# Coffee Demo / P4

This is a real PocketJS reference app on the existing Linux Host, not a browser screenshot. It never controls the heater, pump, payment or dispense service. Eight drinks, two pages, a confirmation modal, simulated progress and six static-label locales establish a frozen P5 workload. The supplied coffee illustrations are original diagnostic fixtures, not product photography approved by the manufacturer.

## Device run

Use the 1024×600 machine already admitted by P3. While idle, pause the existing UI/input consumer using its normal maintenance process. Keep device control, network and OTA running. This program does not stop them for you.

```
tar xzf imx6ul-coffee-demo.tar.gz
cd coffee-demo
sha256sum -c SHA256SUMS
./run-coffee-demo.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /tmp/coffee-media 3600
```

The session is bounded to 3600 logical turns, approximately one minute plus rendering delays. It writes fb0 but does not change display mode, enable VSync/PAN, or restore the previous image. `logs/coffee-*` includes startup, JSON, input/guest/CPU-present CSV and checksums. This is not a production service launcher.

## Replace images while the demo is running

Use a second terminal. The UI polls local store metadata every 30 guest turns, but applies a complete image generation only on the home page with no active gesture. Modal/preparation/active-touch defer activation. UI state is retained; no executable rebuild, UI restart or machine reboot is needed. After application, `MEDIA_APPLIED generation=...` appears in the startup log and the image revision marker changes.

The signed `updates/demo-a.zip` and `demo-b.zip` have visibly different cup colors. `updates/demo.public` is their ephemeral public verification key; the private signing seed is not shipped.

```
./mediactl install /tmp/coffee-media updates/demo.public updates/demo-a.zip
./mediactl install /tmp/coffee-media updates/demo.public updates/demo-b.zip
./mediactl status /tmp/coffee-media
./mediactl rollback /tmp/coffee-media updates/demo.public
```

`MEDIA_INSTALLED` means requested local generation committed, not UI-applied acknowledgment. Confirm `MEDIA_APPLIED` and the screen. The same current generation is used after app restart. `/tmp` is volatile: choose an approved persistent local filesystem for deployment; no device-specific persistent mountpoint is assumed.

### USB

Copy the signed ZIP to an already mounted USB filesystem and invoke the same installer with its actual path. No mountpoint guessing, mounting, or autorun. Images are copied into the local store; USB can be removed after installer success.

```
./mediactl install /tmp/coffee-media updates/demo.public /actual/usb/path/demo-b.zip
```

### HTTPS

Publish the same signed bundle to your own HTTPS endpoint:

```
./mediactl fetch /tmp/coffee-media updates/demo.public https://your-asset-host.example/demo-b.zip
```

This is a URL template, not a configured server. Certificate verification stays enabled. The board needs a valid clock and CA trust store; neither follows from the P3 display test. No redirects, URL credentials or HTTP downgrade. Transfers are bounded to 16 MiB and 90 seconds. Incomplete/invalid downloads do not replace current resources. TLS tests use a certificate-verified local HTTPS fixture, not a validated customer production endpoint.

## Your own images

Use desktop `mediactl` (Windows binary provided) or build `tools/media`. Prepare eight files, one per logical ID: `espresso`, `americano`, `latte`, `cappuccino`, `flatwhite`, `mocha`, `tea`, `water`, with `.png` or `.jpg`. To change one image, keep the other seven in the next complete bundle. Raw images are not compiled into JS. This MVP uses whole-generation updates, not partial packages.

```
mediactl keygen operator
mediactl pack images campaign-001 operator.private campaign-001.zip
```

Keep `operator.private` on the signing workstation, never on the coffee machine, USB delivery medium or public server. Provision `operator.public` through an authorized channel and use it instead of `demo.public`. Do not trust a public key from an incoming bundle. Packaging does not confer copyright permission.

## Resource and trust boundaries

The signed manifest permits exactly eight image IDs, canonical PNG/JPEG paths, hashes, a version and license declarations. Unknown fields, duplicate keys/entries, missing images, extra files, traversal, symbolic links, wrong signature/key, hash mismatch, unsupported codec and excess size are rejected. External packages cannot replace JS, native code, fonts, UI locale definitions or control parameters.

Each image is <=2 MiB encoded, <=2048 pixels on either axis and <=1,048,576 decoded pixels. The installer decodes outside the UI and aspect-fits to **256×128** straight RGBA. Both dimensions are powers of two required by the pinned PocketJS texture ABI. Nearest-neighbor is the MVP resampler; prepare final-size images for best quality. Eight textures contain 1,048,576 pixel bytes, with an additional bounded generation during replacement. This is not measured total RSS. WebP/SVG/GIF/video are unsupported.

The store directory and its parents must be trusted local administration paths, not writable by untrusted users or backed by USB. Installer and UI must use the intended local service identity. A Linux writer lock protects current+previous generations. Files are synced before atomic rename of the current pointer; readers use complete immutable generation files. Validation/download failure retains old resources. An OS/storage error **after pointer rename** can mean uncertain commit status: inspect `mediactl status` rather than assume no change. Board power-loss/fsync durability remains P7/P8. Orphan temporary files may need maintenance after a crash. CRC detects packet corruption, not publisher identity; signature verification is the installer responsibility and local store is trusted.

A rejected new packet retains existing textures. Missing store falls back to built-in images. A rejected generation is not retried every frame: publish a corrected generation or restart after authorized store repair. OOM/process failure and persistent storage corruption are not production-qualified.

## Text capability matrix

| Scope | P4 admission |
| --- | --- |
| zh-CN / en-US / de-DE / fr-FR / es-ES / pt-PT | Fixed NFC application labels in a bounded subset atlas, both viewports |
| Arbitrary Chinese input, traditional Chinese, Japanese, Korean | Not admitted by this subset |
| Arabic / RTL / complex shaping / combining-sequence editing | Not admitted; no reverse-string bidi simulation |
| UI locale / input locale / keyboard layout | Separate; input locale and keyboard layout remain unset |
| IME, preedit, caret, candidate selection | P4A #12, not implemented here |

Font source is pinned Noto Sans CJK SC Sans2.004 with OFL notice. Build checks label codepoints and advances. Only a bitmap subset is loaded, not a full font. Glyph presence is not proof of shaping or bidi; static-label coverage is not international text entry.

## Verification

```
make build-demo
make test-demo-native
make test-demo-arm
make test-assets
make verify-demo
make package-demo
```

Real QuickJS/Core tests cover locale pages, select/modal/cancel, swipe, progress/done, cancellation, resource delivery, busy deferral, malformed packet rejection, repeated texture swaps and actual current-file store reload/rollback/restart behavior. Native/ARM full-frame outputs are compared. Independent Go tests cover signatures, installation, rollback, types, quotas and TLS. P0–P3 gates remain required.

Initial Go1.26.8 ARM test binaries crashed under QEMU6.2; the same binary passed under QEMU8.2.2. The P4 job therefore uses a provenance-recorded modern static emulator while retaining the existing Ubuntu22/GCC ARM compiler baseline. This is an emulator validation change, not a claim that the Go helper has already run on the physical board. Go's minimum-requirements documentation explicitly notes known QEMU versions before7.1: https://go.dev/wiki/MinimumRequirements .

1024×800 is a separate software viewport with hardware still unadmitted. P4 physical appearance, USB copy/removal, board HTTPS trust/network and hot switching need device logs and visual confirmation. P4 does not certify 30FPS, RAM targets, power-cut safety or production release. Resource/scene hashes identify the P5 workload; changing image generation changes benchmark identity.
