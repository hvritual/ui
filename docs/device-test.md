# i.MX6UL physical framebuffer test package

This package is a hardware-gated extension of P2. It does not close #3/#4. The package executable is statically linked for ARMv7 Cortex-A7 hard-float so the first physical-board test does not depend on the board's currently unknown glibc/uClibc version or dynamic loader. The build gate rejects an ELF interpreter and `DT_NEEDED` entries and re-runs the executable under `qemu-arm -cpu cortex-a7` without a sysroot.

Build with `make build-device-package`. Output is `out/device/imx6ul-device-test.tar.gz`.

The package contains:

- `ui-host-imx6ul-static`: real locked PocketJS/QuickJS/Core plus the P2 fbdev Presenter, no fake framebuffer symbols.
- `assets/display-scene.js` and `assets/display-font.bin`: the same diagnostic scene assets exercised by P2.
- `run-probe.sh`: read-only framebuffer probe. It records uname, os-release, cpuinfo, meminfo, framebufer nodes, fbset, executable SHA256, program output and probe JSON.
- `run-display-test.sh`: explicitly gated write test. It first repeats the read-only probe, then writes only after the caller supplies `I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER`.
- build/ELF/QEMU logs, manifest and `SHA256SUMS`.

## On the device

Extract into a writable test directory and run the safe probe first:

```sh
tar xzf imx6ul-device-test.tar.gz
cd imx6ul-device-test
./run-probe.sh /dev/fb0
```

Return the generated `logs/probe-*` directory before display writing if the probe fails or reports an unexpected mode.

After confirming `/dev/fb0` is the intended panel and stopping the existing framebuffer writer through the device's normal service procedure:

```sh
./run-display-test.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 imx6ul-1024x600 300
```

The command does not change display mode, enable pan/vsync, stop/restart other services, restore previous pixels, or validate physical performance. The last test image may remain visible after exit. Preserve the generated `logs/display-*` directory and take a screen photo/video for review.
