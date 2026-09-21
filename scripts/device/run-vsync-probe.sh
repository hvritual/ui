#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$BASE/ui-host-imx6ul-static"
COUNT=${1:-20}
TIMEOUT_MS=${2:-100}
FBDEV=${3:-/dev/fb0}
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "manual-$$")
OUT="$BASE/logs/vsync-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "P2B_DEVICE_VSYNC_BEGIN fbdev=$FBDEV count=$COUNT timeout_ms=$TIMEOUT_MS"
  echo "READ_ONLY: FBIO_WAITFORVSYNC only; no framebuffer write, mode-set or pan."
  echo "=== SYSTEM ==="
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo "=== FBSET ==="
  fbset -fb "$FBDEV" 2>/dev/null || fbset 2>/dev/null || true
  echo "=== BINARY ==="
  ls -l "$BIN" || true
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$BIN"; fi
  echo "=== VSYNC PROBE ==="
  "$BIN" --probe-vsync --fbdev "$FBDEV" --count "$COUNT" --timeout-ms "$TIMEOUT_MS" --output "$OUT/board-vsync.json"
  RC=$?
  echo "VSYNC_EXIT_CODE=$RC"
  if [ -f "$OUT/board-vsync.json" ]; then
    echo "=== VSYNC JSON ==="
    cat "$OUT/board-vsync.json"
  fi
  echo "P2B_DEVICE_VSYNC_END rc=$RC"
  exit "$RC"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
