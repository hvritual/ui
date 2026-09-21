# P2B bounded VSync probe

Status: software implementation and fixture verification. Real i.MX6UL result remains pending until the device probe is executed.

## Scope

The probe uses the standard framebuffer ioctl `FBIO_WAITFORVSYNC` only. It is read-only with respect to framebuffer pixels and display mode:

- no `FBIOPUT_VSCREENINFO`;
- no `FBIOPAN_DISPLAY`;
- no framebuffer mmap/write;
- no automatic enablement of VSync in the production Presenter.

Each wait is executed in a child process and observed through a pipe. The parent enforces a per-sample timeout and kills the child if the driver blocks indefinitely, so a broken/unsupported ioctl cannot hang the diagnostic command.

## CLI

```sh
ui-host --probe-vsync --fbdev /dev/fb0 --count 20 --timeout-ms 100 --output board-vsync.json
```

The JSON classifies the result as `supported`, `unsupported`, `timeout`, `interrupted`, or `error`. For successful waits it records raw nanoseconds and min/median/p95/max. Fixture timing is never accepted as physical refresh evidence.

## Device package

After building:

```sh
make build-device-package
```

the static package contains:

```sh
./run-vsync-probe.sh 20 100 /dev/fb0
```

This command is safe to run without stopping the existing framebuffer writer because it does not write pixels or change display mode. Preserve the generated `logs/vsync-*/startup.log` and `board-vsync.json`.

## Admission decision

- `supported` with stable real-device waits: eligible for a later, separately reviewed Presenter synchronization strategy.
- `unsupported`: keep row-copy and retain tearing risk; do not emulate success.
- `timeout`: treat the ioctl as unsafe for production until the driver behavior is understood.
- `error`/unexpected errno: investigate before any synchronization change.

P2B does not enable page flipping. The observed 1024×600 board still has `yres_virtual == yres == 600`, so PAN/page-flip remains unadmitted.
