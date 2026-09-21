# Linux framebuffer Presenter

Status: software fixture implementation; physical panels and device ABI are not certified. The observed 1024x600 board reports 32 bpp (RGB offsets 16/8/0, no alpha), but its fixed info, active framebuffer, libc and SDK remain unknown. No 1024x800 hardware observation is inherited.

## Interface and supported memory layouts

`HostFrame` is a borrowed, opaque BGRA byte buffer. `host_pump_present` calls a synchronous `HostPresenter` on the same UI thread after rendering; consumers must not retain the pointer. `host_pump` delegates without a presenter and preserves the original guest -> jobs -> core tick ordering and 60-turn / every-two-turn render cadence. A presenter error latches HOST_FAILED. No damage, DRM, GPU or PxP implementation is claimed; the callback is their future boundary.

The fbdev backend queries FBIOGET_FSCREENINFO and FBIOGET_VSCREENINFO. It accepts only little-endian PACKED_PIXELS / TRUECOLOR, standard non-grayscale layouts, upright/non-interlaced modes. 32-bit RGB channels must each be 8 bits, byte-aligned and disjoint; an optional A8 must be disjoint and is written opaque. Unused X bits are zero. RGB565 (offsets 11/5/0, lengths 5/6/5) is a tested software conversion, not a measured panel format. 24-bit, DIRECTCOLOR/palettes, FOURCC, reversed bits, arbitrary packed formats, nonzero rotation and ywrap are rejected.

Every present uses real line_length, virtual dimensions and x/y offsets. Full virtual rows and the last visible byte must fit smem_len, with checked size_t arithmetic and a 64 MiB maximum mapping. The source may have independent padding. Rejections occur before any pixel writes; hidden virtual pixels, row padding and guard areas are preserved. No scaling or automatic rotation is performed: viewport must equal the panel. Logical scale is 1; physical DPI stays unknown, and width_mm/height_mm are only reported.

## Device lifecycle and ownership

Probe uses O_RDONLY, O_NOFOLLOW and no mmap or mutating ioctl. The selected node must be a character framebuffer (major 29). Display testing explicitly uses O_RDWR, an advisory nonblocking exclusive flock and MAP_SHARED. The mapping origin must be page-aligned; unaligned physical framebuffer origins are explicitly unsupported rather than guessing BSP-specific mmap semantics. open/stat/lock/query/map/requery/close/unmap failures have stable error codes and errno. close EINTR is not retried on Linux; failed munmap retains its owner for retry. Success does not suppress cleanup failures.

The fixed/variable information is re-read before each write. An observed geometry/storage/format change stops presenting instead of writing through the stale mapping. This is not an atomic KMS ownership guarantee: the operator must stop other display writers, including any console that redraws there. Advisory flock cannot exclude fbcon or applications that ignore it. This code never stops an existing service, changes device permissions, switches VTs, blanks the screen, changes a mode, or modifies a kernel driver.

Pan capability is reported as a candidate only when ypanstep and virtual rows suggest a second full page. FBIOPAN_DISPLAY is never called. Vsync remains explicitly `not-probed`; the potentially blocking ioctl is not enabled without driver evidence and a bounded wait strategy. The current method is mmap row copying and can tear. Two /dev/fb nodes do not imply double buffering; a software offscreen buffer is not scanout page-flipping. Active pan/vsync validation is a remaining hardware-gated slice, not a completed capability.

## Build and software acceptance

From a clean committed checkout with the locked P1 Rust/GNU/ARM toolchains:

```sh
make check
make check-targets
make test-display-unit
make fetch-runtime
make test-display
make test-display-arm
make verify-runtime
make verify-display
```

`make test-display` also rebuilds and re-tests the native P1 runtime; the ARM variant does likewise. The standalone unit target needs native GCC/Python only and runs ASan/UBSan plus rejection tests. The cloud workflow runs native and QEMU Cortex-A7. Missing dependencies and stale/incomplete evidence fail nonzero.

The display fixture loads an original diagnostic ASCII bitmap atlas through the real Core font API, uploads a 2x2 RGBA image through the real texture API, creates geometry/text/clipping nodes and updates a moving marker via guest frame callbacks. Both 1024x600 and 1024x800 are tested in 32-bit and RGB565 virtual framebuffers with nonzero offsets and padded, deliberately unaligned scanlines. Expected full-frame pixels come from separately authored integer geometry/font patterns, not by blessing the renderer's current output. Initial and updated frames, invisible-byte canaries, callback failure and production CLI dispatch are checked. Deliberately corrupted golden pixels must return exit 1. The font is an ASCII test resource, not multilingual typography/IME coverage.

Only syscall-level fbdev I/O is link-wrapped in tests; Core, QuickJS, layout, software raster and Presenter code are real. The production ui-host contains neither wrappers nor fake device state. A successful test means `real-core-to-fixture-framebuffer`, never physical panel validation. The P1 source lock, features and allocator patch remain unchanged.

## Device commands — only after ABI admission

Current GNU ARM binaries still require the ABI shown in their P1 ELF reports (previous baseline GLIBC 2.35 / /lib/ld-linux-armhf.so.3). These binaries must not be assumed deployable on Buildroot 2019.05-rc1. Do not replace device libc. Once a matching SDK/sysroot build has been admitted, choose the actual framebuffer node explicitly:

```sh
./ui-host --probe-display --fbdev /dev/fb0 --output board-display.json
./ui-host --display-test --fbdev /dev/fb0 --profile imx6ul-1024x600 \
  --asset-root fixtures --ticks 300 --output board-display-test.json
```

From an extracted diagnostic archive the executable is `arm/ui-host` (or `native/ui-host` for a Linux host) and assets are in `fixtures/`. No rootfs installation is required by the archive layout. Omit `--output` to emit JSON to stdout. Output files must be new; existing evidence and symlinks are not overwritten. The probe's `admitted` field is memory-layout admission, not ABI, ownership or physical visual approval. Failures still produce JSON when the output can be written. Errors and diagnostic status go to stderr.

`--display-test` is a deliberate screen write. Run on an isolated test device after stopping other display owners through its existing approved operations procedure. SIGINT/SIGTERM exit through cleanup; at most 600 logical turns are accepted. A final test pattern remains on the framebuffer after exit; the command neither restores old pixels nor restarts the original UI. It makes no FPS, sustained RAM, crash recovery or tear-free claim.

## Remaining per-panel evidence

For each physical model, retain the matching binary SHA/ABI evidence, probe JSON, test JSON, photograph/video including all four color-coded corners, readable text, clip edge, image orientation, grayscale and moving-marker tearing observations. Confirm active node, line_length/smem_len/virtual geometry, permissions/ownership, orientation and physical DPI independently. Runtime mapping success is not a photograph. #4 remains open until both model evidence and any admitted synchronization path are reviewed.

## Primary references

Linux framebuffer API: https://docs.kernel.org/fb/api.html (consulted 2026-09-21), especially packed-pixel padding, bitfields and fixed/variable info. Linux 4.9 UAPI: https://github.com/torvalds/linux/blob/v4.9/include/uapi/linux/fb.h . Driver behavior may differ from generic documentation; no untested driver behavior is promoted to a capability. PocketJS contracts are bound by `toolchains/runtime.lock.json` to revision 53a17f6416c3333f1171141bf996695720101ed2.
