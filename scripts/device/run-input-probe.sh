#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$BASE/input-probe-imx6ul-static"
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "manual-$$")
OUT="$BASE/logs/input-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "P3_INPUT_PROBE_BEGIN"
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo "=== INPUT NODES ==="
  ls -l /dev/input 2>/dev/null || true
  echo "=== PROC INPUT DEVICES ==="
  cat /proc/bus/input/devices 2>/dev/null || true
  echo "=== BINARY ==="
  ls -l "$BIN" || true
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$BIN"; fi
  FOUND=0
  for node in /dev/input/event*; do
    [ -e "$node" ] || continue
    FOUND=1
    name=$(basename "$node")
    echo "=== PROBE $node ==="
    "$BIN" --probe "$node" >"$OUT/$name.json" 2>"$OUT/$name.stderr"
    rc=$?
    echo "INPUT_NODE_EXIT path=$node rc=$rc"
    cat "$OUT/$name.stderr"
    cat "$OUT/$name.json"
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$OUT/$name.json" >"$OUT/$name.sha256"; cat "$OUT/$name.sha256"; fi
  done
  if [ "$FOUND" -eq 0 ]; then
    echo "INPUT_NO_EVENT_NODES"
    exit 1
  fi
  echo "P3_INPUT_PROBE_END"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
