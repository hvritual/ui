# Standby video: isolated decoder and native surface

References #28. #29 (embedded video during active business interaction) is independent. #25 quality/CPU and #27 dynamic-scene physical acceptance remain open. The new `video-host` reuses the real PocketJS Coffee app, Host, evdev, InputBridge and Presenter. It is not a browser, a second framebuffer writer or a device-control service. Existing Coffee/media-scene launchers are unchanged.

## Delivered flow and scope

Idle home -> fullscreen video/image playlist -> consume the complete wake gesture -> restore Coffee. Only the following gesture may select a drink. Coffee still simulates making; no dispense/heater/payment command is introduced. A confirmation/making page prevents standby entry. A native surface is reusable by #29, but an arbitrary PocketJS Video node with business z/clip and concurrent interaction is NOT delivered here.

One decoder child handles one local file at a time. It links unmodified FFmpeg 7.1.5, minimal static avformat/avcodec/avutil/swscale, MOV demuxer and H264 decoder. No network, GPL/nonfree components, encoder or FFmpeg CLI player is shipped. The launcher passes a preopened regular descriptor, never a URL or shell command. Custom AVIO rejects secondary opens. Child address space is bounded to 96MiB, CPU to 120 seconds, and core dumps disabled. These are failure bounds, not measured device budgets. Process separation does not create CPU capacity and is not a security sandbox against compromised root.

Development admission: MP4/H264, progressive 8-bit YUV420, square pixels, no display-rotation matrix, <=640x360, <=30fps, <=60 seconds, <=1800 frames and <=8MiB per video. Muted: audio streams are not decoded/played. No HEVC/AV1, subtitles, seeking, live RTSP/HLS or streaming downloads. Actual frame dimensions, timestamps and formats are checked independently of the manifest. Installer MP4 preflight is NOT decoder acceptance. No sustained i.MX6UL frame-rate claim is made. Fullscreen means the 1024x600 display rectangle, not 1024x600 decoding.

## Ownership and lifecycle

The child writes a framed stdout pipe and cannot inherit framebuffer/input descriptors across exec. Only the Host presents. One reusable BGRA surface and a preallocated receive buffer hold bounded pixels. No per-frame JS ArrayBuffer or whole-image-pack callback is used. Decoder internal reference frames are additional memory; never call two display buffers the total decoder memory.

Frames carry session, sequence, PTS, shape and length. Future frames wait in the bounded receive slot; pipe backpressure stops decode from running ahead. Late display frames may be dropped while necessary compressed reference frames are decoded. First frame anchors CLOCK_MONOTONIC playback; pause shifts the origin. Single/loop lists and mixed still/video items use that clock. EOF requires at least one frame.

Malformed frames, worker exit or 5 seconds without progress stop playback and show the authenticated poster. The failed generation is not spawned repeatedly; install a new valid generation to retry. The diagnostic run reports failure, not a false video PASS. A wake invalidates session/surface and requests child termination without synchronously waiting for teardown. Its complete down/move/up/cancel/held-state is consumed until all-up. Input loss also preempts; rediscovery retains initial-held suppression. No replacement starts while an old child remains unreaped. Cleanup uses bounded WNOHANG polling; SIGKILL cannot guarantee terminating an uninterruptible kernel D-state, which is reported.

SIGUSR2 toggles standby pause/resume. SIGUSR1 invokes a priority-preempt hook, restores business UI and inhibits standby for that diagnostic session. This is not integration with the real machine fault bus. No arbitrary business operation is accepted from media. Guest/Promise turns retain the logical 60Hz contract. Static business frames use clean-frame skips; eligible changed video frames use native contain/bilinear composition and the same Presenter. VSync/PAN/PxP/kernel/BSP remain unchanged. CPU submit completion is not LCD scanout.

## Signed updates

Commands: `video-pack ROOT MANIFEST PRIVATE OUTPUT`, `video-install STORE PUBLIC ZIP`, `video-fetch STORE PUBLIC HTTPS_URL`, `video-rollback STORE PUBLIC`. They reuse Ed25519/SHA256, bounded verified TLS and current/previous activation. A video store must be separate from Coffee/media-scene stores. Trust keys are provisioned independently; private keys never enter board, USB delivery bundles or public media servers.

Each manifest has 2..8 assets (at least one video and image poster), 1..16 playlist items, `idle_ms` 1000..60000, image dwell 500..60000ms. Video duration follows PTS. Images reuse the alpha-safe installer resampler. Bundle and aggregate encoded content are <=16MiB. These are protocol limits, not all-decoded-memory claims.

Install immutable generation files before atomically changing current. The Host pins all active asset descriptors; GC cannot truncate an already open playing file. The Host trusts the owned, non-group/world-writable local store; the installer verifies signatures. Playing generations remain pinned and updates apply only after waking to safe business home. `VIDEO_INSTALLED ... applied=false` is local install; `VIDEO_PLAN_APPLIED` acknowledges Host application; `VIDEO_START` starts actual decoder admission and may still reject a bad stream. USB supplies only the bundle, not continuous playback. /tmp is disposable. Power-cut/fsync/storage corruption are separate production acceptance.

## Run on the already admitted 1024x600 board

Pause the existing UI/input consumer using normal maintenance steps. Keep control/network/OTA services running. This package does not stop services or restore the old screen after exit. Reuse an authorized P4 `labels.atlas` already present on the device; font files are not distributed in this video package.

```sh
cd standby-video
sha256sum -c SHA256SUMS
VIDEO_FONT_ATLAS=/actual/existing-p4/assets/labels.atlas \
./run-standby-video.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /tmp/standby-video-test 60
```

A plays a 320x180/15fps diagnostic moving pattern after 1.5s idle. B mixes a poster, 640x360/15fps main-profile clip and the smaller clip. These are input test profiles, not guaranteed board rates. Observe motion, touch once and fully release (exit only), then touch a drink again. Cancel to home to allow standby again. Pause/resume with `kill -USR2 PID`; priority fallback with `kill -USR1 PID` using the printed Host PID. SIGUSR1 disables standby for the rest of the test.

```sh
./mediactl video-install /tmp/standby-video-test updates/video.public updates/video-b.zip
# Wake and release; observe PLAN_APPLIED, then the next idle entry.
./mediactl status /tmp/standby-video-test
./mediactl video-rollback /tmp/standby-video-test updates/video.public
./mediactl video-fetch /tmp/standby-video-test updates/video.public https://your-host/video-a.zip
```

HTTPS is a template, not a provisioned service; actual CA/time/network remain unverified. USB uses its actual mounted path, never autorun/mount guessing. A/B/once examples and editable JSON are included. Do not mix public keys and bundles from different builds.

## Evidence and remaining acceptance

Native tests use the real FFmpeg child, real clocks and real PocketJS; device I/O alone is a fixture in the production-loop tests. They exercise PTS/pause/loop/exit, whole wake then next gesture, native composition, process-fault fallback and priority preemption. ARM tests run the actual decoder/Core, baseline and main/B-frame clips, session/shape/PTS, bounded pipes/surface/state and Go signature/install/rollback under Cortex-A7 QEMU. Native and ARM decoded samples have explicit integer-rounding parity budgets; restored Core output must match exactly. ARM fork/exec on the physical board and all CPU/FPS claims remain pending. No old P0-P4 test is waived. Intentional failures must fail. Build/test/source/dependency hashes and CI identity are sealed before packaging.

JSON reports UI CPU, lifetime peak RSS, decoder CPU/RSS when reaped, frames/drops, sessions, wake gestures and faults. Child CPU snapshots exclude a running worker until reap; final close/reap supplies the completed counter. CSV separates native compose and Presenter CPU time. Decoder work counters exclude demux/pipe blocking; worker CPU includes decode/demux/conversion. UI/worker peaks are separate maxima, not simultaneous system memory. Other services and mediactl need separate measurement. `physical_video_validated` remains false in automatic reports.

Return logs/video-*/ and a short playback/wake/second-tap video, update/rollback output, CPU frequency/background load and a representative production MP4. Accept sustained decode/display FPS, CPU/RSS, responsiveness and no click-through on the real device. 1024x800 and #29 embedded business video remain separate; #28 is not closed from software-only tests.

## Redistribution materials

The independent decoder-source-relink-kit includes exact unmodified FFmpeg source, configure options, LGPL notices, adapter source/object and libraries for relinking with modified libraries. No GPL encoder or source font is shipped. This does not relicense the independent UI or provide blanket patent/legal clearance. Retain the kit and review obligations before commercial redistribution.
