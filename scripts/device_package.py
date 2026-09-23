#!/usr/bin/env python3
"""Build a self-contained static ARMv7 i.MX6UL framebuffer diagnostic package."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/device/imx6ul"
RUNTIME = ROOT / "out/runtime/arm"
CORE = ROOT / "out/runtime/cargo/armv7-unknown-linux-gnueabihf/release/libpocketjs_symbian_core.a"
DISPLAY_ASSETS = ROOT / "out/display/fixtures"
CC = "arm-linux-gnueabihf-gcc"
READELF = "arm-linux-gnueabihf-readelf"
NM = "arm-linux-gnueabihf-nm"
QEMU = "qemu-arm"


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run(args, log: Path, expected: int = 0, timeout: int = 180) -> str:
    args = [str(x) for x in args]
    log.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=timeout)
    with log.open("a", encoding="utf-8") as f:
        f.write("$ " + shlex.join(args) + "\n")
        f.write(result.stdout)
        f.write(f"\nexit_code={result.returncode}\n")
    print(result.stdout, end="")
    if result.returncode != expected:
        fail(f"command exit {result.returncode}, expected {expected}: {' '.join(args)}")
    return result.stdout


def need(path: Path) -> Path:
    if not path.is_file():
        fail(f"missing prerequisite: {path}")
    return path


def shell_scripts(binary_name: str) -> dict[str, str]:
    common = r'''BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$BASE/%s"
FBDEV=${FBDEV:-/dev/fb0}
STAMP=$(date +%%Y%%m%%d-%%H%%M%%S 2>/dev/null || echo "manual-$$")
mkdir -p "$BASE/logs"
log_system() {
  echo "=== SYSTEM ==="
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo "=== CPU ==="
  cat /proc/cpuinfo 2>/dev/null || true
  echo "=== MEMORY ==="
  cat /proc/meminfo 2>/dev/null || true
  echo "=== FB NODES ==="
  ls -l /dev/fb* 2>/dev/null || true
  echo "=== FBSET ==="
  fbset -fb "$FBDEV" 2>/dev/null || fbset 2>/dev/null || true
  echo "=== BINARY ==="
  ls -l "$BIN" || true
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$BIN"; fi
}
''' % binary_name
    probe = "#!/bin/sh\nset -u\n" + common + r'''
OUT="$BASE/logs/probe-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "P2_DEVICE_PROBE_BEGIN fbdev=$FBDEV"
  log_system
  echo "=== PROBE ==="
  "$BIN" --probe-display --fbdev "$FBDEV" --output "$OUT/board-display.json"
  RC=$?
  echo "PROBE_EXIT_CODE=$RC"
  if [ -f "$OUT/board-display.json" ]; then
    echo "=== PROBE JSON ==="
    cat "$OUT/board-display.json"
  fi
  echo "P2_DEVICE_PROBE_END rc=$RC"
  exit "$RC"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
'''
    display = "#!/bin/sh\nset -u\n" + common + r'''
TOKEN=${1:-}
if [ "$TOKEN" != "I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER" ]; then
  echo "Refusing display write. Usage: $0 I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [fbdev] [profile] [ticks]" >&2
  exit 2
fi
FBDEV=${2:-$FBDEV}
PROFILE=${3:-imx6ul-1024x600}
TICKS=${4:-300}
case "$PROFILE" in
  imx6ul-1024x600|imx6ul-1024x800) ;;
  *) echo "Unsupported profile: $PROFILE" >&2; exit 2 ;;
esac
OUT="$BASE/logs/display-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "P2_DEVICE_DISPLAY_BEGIN fbdev=$FBDEV profile=$PROFILE ticks=$TICKS"
  echo "WARNING: this command writes the framebuffer; stop other display owners first."
  log_system
  echo "=== PRE-WRITE PROBE ==="
  "$BIN" --probe-display --fbdev "$FBDEV" --output "$OUT/board-display.json"
  PROBE_RC=$?
  echo "PROBE_EXIT_CODE=$PROBE_RC"
  if [ -f "$OUT/board-display.json" ]; then cat "$OUT/board-display.json"; fi
  if [ "$PROBE_RC" -ne 0 ]; then
    echo "DISPLAY_SKIPPED probe_failed"
    exit "$PROBE_RC"
  fi
  echo "=== DISPLAY TEST ==="
  "$BIN" --display-test --fbdev "$FBDEV" --profile "$PROFILE" \
    --asset-root "$BASE/assets" --ticks "$TICKS" --output "$OUT/board-display-test.json"
  RC=$?
  echo "DISPLAY_EXIT_CODE=$RC"
  if [ -f "$OUT/board-display-test.json" ]; then
    echo "=== DISPLAY TEST JSON ==="
    cat "$OUT/board-display-test.json"
  fi
  echo "P2_DEVICE_DISPLAY_END rc=$RC"
  exit "$RC"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
'''
    return {"run-probe.sh": probe, "run-display-test.sh": display}


def main() -> int:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    for exe in [CC, READELF, NM, QEMU]:
        if shutil.which(exe) is None:
            fail(f"missing tool: {exe}")

    names = ["host.o", "platform.o", "display-presenter.o", "display-fbdev.o", "display-cli.o", "display-vsync.o", "media-store.o",
             "input-state.o", "input-live.o", "input-bridge.o", "input-cli.o",
             "main-host.o", "runtime-host.o", "personality.o", "libquickjs.a"]
    inputs = [need(RUNTIME / name) for name in names] + [need(CORE)]
    binary = OUT / "ui-host-imx6ul-static"
    cflags = ["-mcpu=cortex-a7", "-mfpu=neon-vfpv4", "-mfloat-abi=hard"]
    run([CC, *cflags, "-static", "-Wl,--gc-sections", "-o", binary, *inputs,
         "-lm", "-ldl", "-lpthread", "-lrt"], OUT / "build.log")

    elf = run([READELF, "-h", "-A", "-l", "-d", "-V", binary], OUT / "elf.log")
    if "Class:                             ELF32" not in elf or "Machine:                           ARM" not in elf:
        fail("static package is not ELF32 ARM")
    if "Tag_ABI_VFP_args: VFP registers" not in elf:
        fail("static package is not ARM hard-float")
    if "Requesting program interpreter" in elf or "Shared library:" in elf or "NEEDED" in elf:
        fail("device package unexpectedly has a dynamic loader/dependency")

    symbols = run([NM, binary], OUT / "symbols.log")
    for name in ["JS_Eval", "pocket_runtime_tick", "pocket_runtime_tick_contacts",
                 "fbdev_present", "display_cli", "vsync_probe_fd", "vsync_cli",
                 "input_cli", "input_live_discover", "input_bridge_ingest", "host_turn_contacts"]:
        if name not in symbols:
            fail(f"missing runtime/display symbol: {name}")
    if "fake_fb" in symbols or "__wrap_" in symbols:
        fail("test fixture symbol leaked into device executable")

    fixtures = ROOT / "out/runtime/fixtures"
    run([QEMU, "-cpu", "cortex-a7", binary, "--profile", "imx6ul-1024x600",
         "--asset-root", fixtures, "--bundle", "scene.js", "--pack", "scene.pak", "--ticks", "6"], OUT / "qemu-headless.log")
    rejected = run([QEMU, "-cpu", "cortex-a7", binary, "--probe-display", "--fbdev", "/dev/null"],
                   OUT / "qemu-probe-negative.log", expected=1)
    if "FB_NOT_FRAMEBUFFER" not in rejected:
        fail("probe failure did not emit the expected framebuffer rejection")
    vsync_report = OUT / "qemu-vsync-null.json"
    vsync_rejected = run([QEMU, "-cpu", "cortex-a7", binary, "--probe-vsync", "--fbdev", "/dev/null",
                          "--count", "2", "--timeout-ms", "20", "--output", vsync_report],
                         OUT / "qemu-vsync-negative.log", expected=1)
    if "VSYNC_PROBE_ERROR" not in vsync_rejected:
        fail("vsync CLI failure did not emit the expected diagnostic")
    vsync_json = json.loads(vsync_report.read_text())
    if vsync_json["operation"] != "vsync-probe" or vsync_json["writes_framebuffer"] is not False:
        fail("vsync device probe scope mismatch")

    assets = OUT / "assets"; assets.mkdir()
    for name in ["display-scene.js", "display-font.bin"]:
        shutil.copy2(need(DISPLAY_ASSETS / name), assets / name)
    shutil.copy2(need(ROOT / "tests/input/touch-scene.js"), assets / "touch-scene.js")

    for name, body in shell_scripts(binary.name).items():
        path = OUT / name
        path.write_text(body, encoding="utf-8")
        path.chmod(0o755)
    vsync_script = OUT / "run-vsync-probe.sh"
    shutil.copy2(ROOT / "scripts/device/run-vsync-probe.sh", vsync_script)
    vsync_script.chmod(0o755)
    touch_script = OUT / "run-touch-test.sh"
    shutil.copy2(ROOT / "scripts/device/run-touch-test.sh", touch_script)
    touch_script.chmod(0o755)
    binary.chmod(0o755)

    readme = OUT / "README.txt"
    readme.write_text(
        "i.MX6UL P2 framebuffer device test package\n\n"
        "1) Safe read-only framebuffer probe:\n   ./run-probe.sh /dev/fb0\n\n"
        "1b) Safe read-only VSync probe:\n   ./run-vsync-probe.sh 20 100 /dev/fb0\n\n"
        "2) Review the probe log/JSON and stop any existing framebuffer writer.\n\n"
        "3) Explicit display write (1024x600 default):\n"
        "   ./run-display-test.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 imx6ul-1024x600 300\n\n"
        "4) Explicit live touch + display test (1024x600 only):\n"
        "   ./run-touch-test.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 /dev/input 1200\n\n"
        "The display/touch tests leave the final diagnostic frame on screen. They do not restore old pixels or restart an existing UI.\n"
        "Send back the generated logs/ directory plus a screen photo/video.\n", encoding="utf-8")

    tracked = [binary, readme, OUT / "run-probe.sh", OUT / "run-display-test.sh",
               OUT / "run-vsync-probe.sh", OUT / "run-touch-test.sh",
               assets / "display-scene.js", assets / "display-font.bin", assets / "touch-scene.js",
               OUT / "build.log", OUT / "elf.log", OUT / "symbols.log",
               OUT / "qemu-headless.log", OUT / "qemu-probe-negative.log", OUT / "qemu-vsync-negative.log", OUT / "qemu-vsync-null.json"]
    sums = OUT / "SHA256SUMS"
    sums.write_text("".join(f"{sha256(p)}  {p.relative_to(OUT)}\n" for p in tracked), encoding="utf-8")
    manifest = {
        "schema_version": 1,
        "target": "i.MX6UL Cortex-A7 ARMv7 hard-float",
        "binary": binary.name,
        "binary_sha256": sha256(binary),
        "static": True,
        "dynamic_loader": None,
        "shared_libraries": [],
        "profiles": ["imx6ul-1024x600", "imx6ul-1024x800"],
        "physical_panel_validated": False,
        "vsync_probe": "read-only-bounded",
        "touch_test": {
            "profile": "imx6ul-1024x600",
            "device_selector": "ilitek_ts+protocol-b-capabilities",
            "transform": {"swap_xy": False, "invert_x": False, "invert_y": False},
            "physical_touch_validated": False,
        },
        "warning": "display-test writes the framebuffer and leaves the final image on screen",
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    package = ROOT / "out/device/imx6ul-device-test.tar.gz"
    package.unlink(missing_ok=True)
    with tarfile.open(package, "w:gz", format=tarfile.PAX_FORMAT) as tar:
        for p in sorted(OUT.rglob("*")):
            tar.add(p, arcname=Path("imx6ul-device-test") / p.relative_to(OUT), recursive=False)
    print(f"DEVICE_PACKAGE_OK binary_sha256={manifest['binary_sha256']} package={package}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        print(f"DEVICE_PACKAGE_FAILED: {e}", file=sys.stderr)
        raise SystemExit(1)
