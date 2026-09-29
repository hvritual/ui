#!/bin/sh
# Diagnostic runner only: does not stop any service or control the machine.
set -u
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
TOKEN=${1:-}
if [ "$TOKEN" != I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER ]; then
  echo "Usage: $0 I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [imx6ul-1024x600|imx6ul-1024x800] [seconds] [explicit touch options]" >&2
  exit 2
fi
shift
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
  "$BASE/ui-framework" --profile "$PROFILE" --asset-root "$BASE/assets" \
     --output "$RUN/runtime" --seconds "$SECONDS" --media-store "$STORE" \
     --fbdev "${FRAMEWORK_FBDEV:-/dev/fb0}" --input-dir "${FRAMEWORK_INPUT_DIR:-/dev/input}" \
     --items "${FRAMEWORK_ITEMS:-8}" --allow-write "$TOKEN" "$@"
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
