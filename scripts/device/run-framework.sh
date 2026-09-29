#!/bin/sh
# Diagnostic runner only: does not stop any service or control the machine.
set -u
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
PROFILE=${1:-imx6ul-1024x600}; [ "$#" -eq 0 ] || shift
SECONDS=${1:-180}; [ "$#" -eq 0 ] || shift
case "$PROFILE" in imx6ul-1024x600|imx6ul-1024x800) ;; *) exit 2;; esac
(cd "$BASE" && sha256sum -c SHA256SUMS) || exit 1
mkdir -p "$BASE/logs" || exit 1
RUN="$BASE/logs/framework-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$RUN" || exit 1
STORE=${FRAMEWORK_STORE_DIR:-$BASE/media-store}
mkdir -p "$STORE" || exit 1
(
  echo "F6A_DIAGNOSTIC_BEGIN profile=$PROFILE"
  echo "WARNING: operator must have stopped competing display/input consumers, not machine control/network/OTA."
  echo "No kernel, libc, display mode, device control or existing service will be changed."
  uname -a
  cat /proc/meminfo
  echo "=== PROGRAM HASH ==="
  sha256sum "$BASE/ui-framework"
  echo "=== BUILD MANIFEST ==="
  cat "$BASE/manifest.json"
  echo "MEDIA_STORE=$STORE"
  echo "=== INPUT INVENTORY (read only) ==="
  echo "INPUT_DIR=${FRAMEWORK_INPUT_DIR:-/dev/input}"
  ls -ld "${FRAMEWORK_INPUT_DIR:-/dev/input}" 2>/dev/null || true
  ls -ln "${FRAMEWORK_INPUT_DIR:-/dev/input}" 2>/dev/null || true
  cat /proc/bus/input/devices 2>/dev/null || true
  for NAME in /sys/class/input/event*/device/name; do
    [ -f "$NAME" ] || continue
    printf '%s: ' "$NAME"
    cat "$NAME" 2>/dev/null || true
  done
  "$BASE/ui-framework" --profile "$PROFILE" --asset-root "$BASE/assets" \
     --output "$RUN/runtime" --seconds "$SECONDS" --media-store "$STORE" \
     --fbdev "${FRAMEWORK_FBDEV:-/dev/fb0}" --input-dir "${FRAMEWORK_INPUT_DIR:-/dev/input}" \
     --items "${FRAMEWORK_ITEMS:-8}" --physical "$@"
  RC=$?
  echo "FRAMEWORK_PROCESS_EXIT=$RC"
  exit "$RC"
) >"$RUN/startup.log" 2>&1
RC=$?
(cd "$RUN" && find . -type f ! -name SHA256SUMS -exec sha256sum '{}' \;) >"$RUN/SHA256SUMS"
cat "$RUN/startup.log"
printf '\nLOG_DIR=%s\n' "$RUN"
echo "Return this directory and an actual LCD/touch video. Automatic reports do not attest visual/HIL acceptance."
exit "$RC"
