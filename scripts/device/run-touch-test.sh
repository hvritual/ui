#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$BASE/ui-host-imx6ul-static"
TOKEN=${1:-}
FBDEV=${2:-/dev/fb0}
INPUT_DIR=${3:-/dev/input}
TICKS=${4:-1200}
if [ "$TOKEN" != "I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER" ]; then
  echo "Refusing touch display test. Usage: $0 I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [fbdev] [input_dir] [ticks]" >&2
  exit 2
fi
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "manual-$$")
OUT="$BASE/logs/touch-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "P3_02_TOUCH_BEGIN fbdev=$FBDEV input_dir=$INPUT_DIR ticks=$TICKS"
  echo "WARNING: test only while idle. Stop existing UI and input consumers; keep device-control services running. No dispense or actuator command is sent."
  echo "Expected interaction: tap A top-left, B top-right, C center, D bottom-left, E bottom-right; then drag and hold. Each target must count once after release."
  echo "=== SYSTEM ==="
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo "=== INPUT DEVICES ==="
  cat /proc/bus/input/devices 2>/dev/null || true
  ls -l "$INPUT_DIR" 2>/dev/null || true
  echo "=== FBSET ==="
  fbset -fb "$FBDEV" 2>/dev/null || fbset 2>/dev/null || true
  echo "=== BINARY ==="
  ls -l "$BIN" || true
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$BIN"; fi

  echo "=== READ-ONLY DISPLAY PROBE ==="
  "$BIN" --probe-display --fbdev "$FBDEV" --output "$OUT/display-probe.json"
  DRC=$?
  echo "DISPLAY_PROBE_EXIT_CODE=$DRC"
  if [ -f "$OUT/display-probe.json" ]; then cat "$OUT/display-probe.json"; fi
  if [ "$DRC" -ne 0 ]; then
    echo "TOUCH_SKIPPED display_probe_failed"
    exit "$DRC"
  fi

  echo "=== TOUCH TEST ==="
  "$BIN" --touch-test --profile imx6ul-1024x600     --fbdev "$FBDEV" --input-dir "$INPUT_DIR"     --asset-root "$BASE/assets" --ticks "$TICKS"     --output "$OUT/touch-test.json" --trace-output "$OUT/input-guest-present.csv"
  RC=$?
  echo "TOUCH_EXIT_CODE=$RC"
  if [ -f "$OUT/touch-test.json" ]; then
    echo "=== TOUCH TEST JSON ==="
    cat "$OUT/touch-test.json"
  fi
  echo "P3_02_TOUCH_END rc=$RC"
  exit "$RC"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
if command -v sha256sum >/dev/null 2>&1; then (cd "$OUT" && sha256sum startup.log display-probe.json touch-test.json input-guest-present.csv >SHA256SUMS) || true; fi
exit "$RC"
