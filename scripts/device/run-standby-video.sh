#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" != I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER ]; then
 echo 'Usage: run-standby-video.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [trusted-local-store] [seconds 5..300]' >&2
 exit 2
fi
STORE=${2:-/tmp/standby-video-store}
SECONDS=${3:-60}
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)
OUT="$BASE/logs/video-$STAMP-$$"
umask 077
mkdir -p "$OUT" || exit 1
LOG="$OUT/startup.log"
if [ ! -f "$BASE/assets/labels.atlas" ]; then
 if [ ! -f "${VIDEO_FONT_ATLAS:-}" ]; then
  echo 'Set VIDEO_FONT_ATLAS to your existing authorized P4 assets/labels.atlas before running.' >&2
  exit 2
 fi
 cp "$VIDEO_FONT_ATLAS" "$BASE/assets/labels.atlas" || exit 1
 chmod 600 "$BASE/assets/labels.atlas"
fi
{
 echo 'STANDBY_VIDEO_BEGIN'
 echo 'WARNING: idle maintenance machine only. Stop original display/input consumers, KEEP control/network/OTA running.'
 echo 'No video audio, device actuation, display mode change, VSync or PAN.'
 uname -a
 cat /etc/os-release 2>/dev/null || true
 cat /proc/meminfo 2>/dev/null || true
 cat /proc/bus/input/devices 2>/dev/null || true
 cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || true
 cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
 echo '=== BINARY AND EXTERNAL FONT HASHES ==='
 sha256sum "$BASE/assets/labels.atlas"
 sha256sum "$BASE/video-host" "$BASE/video-decoder" "$BASE/mediactl"
} >"$LOG" 2>&1
if [ ! -e "$STORE/current" ]; then
 "$BASE/mediactl" video-install "$STORE" "$BASE/updates/video.public" "$BASE/updates/video-a.zip" >>"$LOG" 2>&1
 rc=$?
 if [ "$rc" -ne 0 ]; then cat "$LOG"; exit "$rc"; fi
fi
"$BASE/video-host" --assets "$BASE/assets" --store "$STORE" --decoder "$BASE/video-decoder" \
 --output "$OUT/video.json" --trace "$OUT/video.csv" --seconds "$SECONDS" >>"$LOG" 2>&1 &
PID=$!
printf '%s\n' "$PID" >"$OUT/ui.pid"
echo "VIDEO_HOST_PID=$PID LOG_DIR=$OUT"
echo 'Wait on homepage for standby video. First touch exits ONLY; lift fully, then second tap may select a drink.'
echo 'Pause/resume from a second terminal: kill -USR2 <VIDEO_HOST_PID>. Priority fallback: kill -USR1 <VIDEO_HOST_PID>.'
trap 'kill -TERM "$PID" 2>/dev/null || true' INT TERM
wait "$PID"
RC=$?
trap - INT TERM
{
 echo "VIDEO_EXIT_CODE=$RC"
 if [ -f "$OUT/video.json" ]; then cat "$OUT/video.json"; fi
 echo "STANDBY_VIDEO_END rc=$RC"
} >>"$LOG"
(cd "$OUT" && sha256sum startup.log video.json video.csv ui.pid >SHA256SUMS) 2>/dev/null || true
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
