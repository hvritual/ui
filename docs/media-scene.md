# Dynamic image elements and carousel

Refs #27; video is deliberately separate (#28 standby, #29 embedded business).
Base: render-quality/idle-presentation fix #26. Its physical CPU/visual acceptance
(#25) remains open and is not a prerequisite for this software slice.

## Delivered scope

`media-scene` is a separate reference application on the same real PocketJS,
evdev and framebuffer Host, not a browser mock and not a replacement for Coffee
business logic. A reusable trusted `createMediaScene` component consumes signed,
data-only snapshots. Update a snapshot to add/remove/move/resize/hide/reorder image
or carousel elements without changing JS, restarting the UI or rebooting the board.
Changing image elements does not create drinks, recipes or actuator commands.

Resources have custom IDs, independent of element IDs and playlist items. The
Coffee v1 eight-slot bundle still works with its original commands. Scene bundles
use separate `scene-*` commands and a separate local store. Mixed-app installation
into one existing store is rejected. No fonts, text/IME definitions, executable
code, arbitrary native props or business actions are accepted in a scene bundle.

## Limits and layout

- 1..16 PNG/JPEG resources; 0..8 elements (empty snapshot clears all media).
- Each resource declares power-of-two texture width/height 16..512. Encoded source
  <=2MiB, source dimensions <=2048 and source pixels <=1,048,576.
- All prepared pixels together <=1.75MiB; metadata <=32KiB; resource event <2MiB.
  The installer reuses the corrected area/bilinear alpha-safe resampler.
- This first bounded protocol eagerly uploads all declared textures, not an
  unbounded/on-demand media library. A replacement temporarily holds old and new
  textures plus packet/copy allocations; these byte budgets are NOT measured RSS.
- Elements use a protected 1024x416 design region. On the 1024x600 screen the
  region begins at y=96 and ends before the trusted controls. The 1024x800
  software viewport scales the region's height. All rectangles must stay inside.
- `fit=contain|stretch`, stable z/order, `visible` true/false. No arbitrary crop,
  cover fit, nested layout or external click handlers in this delivery.
- Image: exactly one item, loop=false. Carousel: 1..16 items, each `hold_ms`
  500..60000, loop=true/false. Switching is a cut; no fade or slide transition.

Texture dimensions and final display rectangles are separate. Prepare images for
actual intended display size/ratio; low-resolution images enlarged by the Core
are not guaranteed photorealistic. This task does not certify the LCD's sharpness.

## Playback and lifecycle

The Host supplies CLOCK_MONOTONIC milliseconds in a 16-byte `PUITICK1` data event
before the normal guest turn. It does not add extra Core ticks or replace the
60Hz guest/Promise/input contract. The component advances by elapsed time, not by
assuming each guest turn lasted 1/60 second. A large clock jump traverses at most
one bounded playlist after modular reduction; a once-only list holds its last
image. Paused/hidden/active-touch content does not advance. Previous/next resets
the current dwell, and pause/resume preserves remaining time.

Only an actual slide or scene change modifies image nodes. Unchanged frames keep
Core damage clean and reuse #26's optional-presentation skip. A changed frame
still uses a full visible framebuffer submission: this is NOT end-to-end dirty
rectangle, VSync, PAN, GPU, PxP or video work.

Snapshots prepare all textures and a detached node subtree before attaching the
new generation and releasing old nodes/textures on the UI thread. Allocation or
validation failure preserves the old scene. Updates wait while a gesture is held
or cancellation is awaiting all-up; control gestures are consumed, never passed
through to media nodes. New scenes restart playlists at item 0 and retain the
user's global paused state. This is whole-snapshot replacement, not per-node patch
merging or a visual editor. Node handles are runtime-private, not stable identities.

## Device run (1024x600 admitted machine)

While the machine is idle, pause the original UI/input consumer using its normal
maintenance procedure. Keep control/network/OTA services running. The test has no
actuator access, but it writes fb0; it neither manages other services nor restores
the previous screen image after exit.

```sh
tar xzf imx6ul-media-scene.tar.gz
cd media-scene
sha256sum -c SHA256SUMS
./run-media-scene.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /tmp/media-scene-store 3600
```

The bounded session lasts 3600 logical turns (about one minute, subject to load
and the existing wall deadline). Use Pause/Resume, Previous and Next in the
trusted footer. Scene A starts with two elements and nine distinct resource IDs;
scene B moves/reorders the carousel, removes the old badge, and adds two elements.

In a second terminal, from the same unpacked directory:

```sh
./mediactl scene-install /tmp/media-scene-store updates/scene.public updates/scene-a.zip
# Observe scene A, then install B while the process is still running.
./mediactl scene-install /tmp/media-scene-store updates/scene.public updates/scene-b.zip
./mediactl status /tmp/media-scene-store
# After B is visible, return to A.
./mediactl scene-rollback /tmp/media-scene-store updates/scene.public
```

Additional supplied snapshots demonstrate hiding an element (`scene-hidden.zip`)
and deleting all media (`scene-empty.zip`). A successful installer reports
`SCENE_INSTALLED ... applied=false`; actual UI application is recorded as
`MEDIA_APPLIED generation=...`. Verify the version label and screen, not merely
installer success. Changes are polled at idle gesture-safe checkpoints. Install
success copies content locally; USB need not remain inserted afterwards.

USB: replace the ZIP argument with the actual already-mounted path; no mount or
autorun. HTTPS: `mediactl scene-fetch STORE PUBLIC_KEY https://your-host/scene.zip`
uses the existing verified TLS, no redirects/credentials/downgrade, bounded time
and size. No production endpoint or device CA store is configured by this task.
The store and its parent path must be trusted local administrative paths; /tmp is
only disposable testing storage, not a persistent production location.

## Author a snapshot

`examples/scene-a.json`, `scene-b.json` and `examples/images/` are editable inputs.
Fields are described in `contracts/media-scene.schema.json`. JSON schema is a
structural description; reference integrity, rectangle containment and aggregate
pixel budgets are additionally enforced by the Go validator and trusted runtime.

```sh
# Run on your signing workstation, not a shared coffee machine.
mediactl keygen operator
mediactl scene-pack examples examples/scene-b.json operator.private campaign.zip
# Provision operator.public through an authorized channel, then install.
mediactl scene-install STORE operator.public campaign.zip
```

`scene-pack` fills hashes, signs the manifest and validates the resulting bundle.
Never take a trust key from an incoming ZIP. Never copy private seeds into device
packages/USB/media servers. Demo A/B/hidden/empty use one ephemeral public key;
do not mix them with a different build's demo key. `status`/rollback are versioned
local-store operations. Power loss after rename/fsync uncertainty and storage
corruption remain P7/P8; no production durability claim is made here.

## Verification and remaining acceptance

Build on the pinned cloud toolchain after `make fetch-runtime build-demo` and
both runtime builds:

```sh
make build-scene
make test-scene-native test-scene-arm
make test-assets
make verify-scene
make build-device-package package-scene
```

Real Core tests cover both viewports, actual image pixels, timed cuts, idle skip,
control contacts, busy deferral, added/removed/hidden elements, bad packet retention,
repeated generations, cleanup, native current-file loading/rejection and rollback.
A separate model clock suite stresses large jumps and allocation failure; it is
not counted as real Core acceptance. Go tests cover signatures, quotas, invalid
references/types, installation and rollback. All prior P0-P4 gates remain required.

Return `logs/scene-*/` plus before/after scene photos or a short carousel video.
CPU/RSS statistics cover the UI process, not the separately invoked installer;
no board FPS, CPU savings, video or 1024x800 physical acceptance is asserted.
