#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROBE="$BASE/input-probe-imx6ul-static"
TRACE="$BASE/input-trace-imx6ul-static"
NODE=${1:-/dev/input/event1}
DURATION_MS=${2:-15000}
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "manual-$$")
OUT="$BASE/logs/trace-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"

(
  echo "P3_01_TRACE_BEGIN node=$NODE duration_ms=$DURATION_MS"
  echo "Sequence during capture: tap TOP-LEFT, tap CENTER, tap BOTTOM-RIGHT, then drag TOP-LEFT -> BOTTOM-RIGHT."
  echo "Only numeric evdev type/code/value and timestamps are stored; no text content."
  echo "=== SYSTEM ==="
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo "=== INPUT DEVICES ==="
  cat /proc/bus/input/devices 2>/dev/null || true
  echo "=== CAPABILITY RE-PROBE ==="
  "$PROBE" --probe "$NODE" >"$OUT/capability.json" 2>"$OUT/capability.stderr"
  PRC=$?
  echo "CAPABILITY_EXIT_CODE=$PRC"
  cat "$OUT/capability.stderr"
  cat "$OUT/capability.json"
  if [ "$PRC" -ne 0 ]; then
    echo "TRACE_SKIPPED capability_probe_failed"
    exit "$PRC"
  fi
  if ! grep -q '"name":"ilitek_ts"' "$OUT/capability.json" ||
     ! grep -q '"mt_protocol_b_candidate":true' "$OUT/capability.json"; then
    echo "TRACE_SKIPPED unexpected_device_or_protocol"
    exit 3
  fi
  if ! command -v sha256sum >/dev/null 2>&1; then
    echo "TRACE_SKIPPED sha256sum_missing"
    exit 4
  fi
  CAP_HASH=$(sha256sum "$OUT/capability.json" | awk '{print $1}')
  echo "CAPABILITY_SHA256=$CAP_HASH"
  echo "=== CAPTURE START ==="
  "$TRACE" --capture "$NODE" --name ilitek_ts --capability-hash "$CAP_HASH"     --duration-ms "$DURATION_MS" --max-events 20000 --output "$OUT/input-trace.json"     >"$OUT/trace.stdout" 2>"$OUT/trace.stderr"
  RC=$?
  echo "TRACE_EXIT_CODE=$RC"
  cat "$OUT/trace.stderr"
  if [ -f "$OUT/input-trace.json" ]; then
    sha256sum "$OUT/input-trace.json" >"$OUT/input-trace.sha256"
    cat "$OUT/input-trace.sha256"
  fi
  echo "P3_01_TRACE_END rc=$RC"
  exit "$RC"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
