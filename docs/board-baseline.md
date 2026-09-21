# Device baseline and ABI admission

Source: command output supplied by the user on 2026-09-21. Capture time, workload
and whether the original production UI was running are not known. The saved
transcription preserves command values with whitespace normalized; its SHA256 is
bound by `targets/boards/myimx6ek140-1024x600.json` and `make check-targets`.

| Item | Observed / interpretation |
|---|---|
| Platform | i.MX6 UltraLite device-tree identification; ARMv7, implementer 0x41 / part 0xc07 |
| Kernel | 4.9.88_V1.0.1.200613_V0.1.3.200624; build string contains SMP PREEMPT; only processor 0 appears in supplied cpuinfo |
| OS | Buildroot 2019.05-rc1; this label does not establish libc family/version or userland float ABI |
| CPU features | NEON, VFPv3/VFPv4 and integer division reported; BogoMIPS 12.00 is not CPU MHz |
| Linux-visible RAM | 242976 KiB = 237.28125 MiB; do not record physical RAM as proven 256 MiB |
| Available snapshot | 171580 KiB ≈ 167.56 MiB, not a guaranteed UI memory budget |
| Swap / CMA | Reported as 0; do not infer accelerator capability solely from this |
| Display | fbset geometry 1024×600, virtual 1024×600, 32 bpp; R/G/B offsets 16/8/0, no alpha; 58.586 Hz reported |
| Device nodes | fb0, fb1; input event0/event1/mouse nodes; their roles/protocols are not established |

One logical 32-bpp buffer is 2,457,600 bytes (2.34375 MiB) at 1024×600;
1024×800 would require 3,276,800 bytes (3.125 MiB), but the latter is only the
confirmed product resolution, not a measurement of this board. Neither arithmetic
includes framebuffer padding, renderer caches, guest/core state, font/image assets
or an input method. `accel false` is the fbset flag, not proof that every hardware
accelerator is absent. Two fb nodes are not proof of double buffering. The supplied
virtual height does not establish page-flip capacity. No display format changes
or writes to `/dev/fb*` are performed in P1.

## Build ABI versus board ABI

P0's measured GNU binary required GLIBC 2.35. P1 emits a fresh readelf report for
its own host; do not simply inherit the P0 floor. Rust's platform support entry
for armv7-unknown-linux-gnueabihf lists a Linux 3.2 / glibc 2.17 baseline, but that
minimum is not the dependencies of a binary linked against a newer sysroot:
https://doc.rust-lang.org/rustc/platform-support.html

The supplied kernel/CPU data supports continuing ARMv7 userspace integration.
It does not prove the loader, C library or vendor drivers are compatible.
`board_admission` therefore remains `blocked-libc-loader-sdk-unverified`.
The 1024×800 profile has no board record; none of the 600-panel measurements are
copied into it. P0's historical `targets/imx6ul.json` remains its unchanged build
baseline; the P1 observation records are authoritative for the supplied hardware.

No extra device commands are required to execute the current cloud/headless slice.
Before a real board is admitted, compare a candidate ELF's architecture/float ABI,
interpreter, NEEDED libraries and symbol versions with an available vendor SDK or
root filesystem. Rebuild against that SDK when needed; do not upgrade the device
libc, overwrite its UI, replace its control/network services or silently switch to
musl/static linking to conceal an unresolved compatibility question.

## Next hand-offs

P2 must probe framebuffer line_length/smem_len/offsets, exact active node, pixel
format and supported synchronization before mapping it. P3 identifies actual
input capabilities rather than assuming event0 is touch. P5 budgets total UI+IME
memory under representative machine workload. These remain separate tests.
