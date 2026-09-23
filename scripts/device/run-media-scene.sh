#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" != I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER ]; then
 echo 'Usage: run-media-scene.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [media-store] [ticks]' >&2
 exit 2
fi
STORE=${2:-/tmp/media-scene-store}
TICKS=${3:-3600}
STAMP=$(date +%Y%m%d-%H%M%S)-$$
OUT="$BASE/logs/scene-$STAMP"
mkdir -p "$OUT"
(
 echo "SCENE_DEMO_BEGIN store=$STORE ticks=$TICKS"
 echo 'Run only while idle; pause original UI/input consumer. Keep machine control/network/OTA running.'
 echo 'Simulation only. No heater/dispense command. USB is never mounted or executed by this program.'
 echo 'Label this run idle / interaction / media-update when returning logs. CPU excludes the separate mediactl process.'
 uname -a
 cat /proc/meminfo 2>/dev/null || true
 cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || true
 sha256sum "$BASE/ui-host-imx6ul-static" "$BASE/mediactl"
 "$BASE/ui-host-imx6ul-static" --touch-test --app media-scene --profile imx6ul-1024x600 \
  --fbdev /dev/fb0 --input-dir /dev/input --asset-root "$BASE/assets" --media-store "$STORE" \
  --ticks "$TICKS" --output "$OUT/scene.json" --trace-output "$OUT/input-guest-present.csv"
 RC=$?
 echo "SCENE_EXIT_CODE=$RC"
 [ ! -f "$OUT/scene.json" ] || cat "$OUT/scene.json"
 exit "$RC"
) >"$OUT/startup.log" 2>&1
RC=$?
cat "$OUT/startup.log"
(cd "$OUT" && sha256sum startup.log scene.json input-guest-present.csv >SHA256SUMS) || true
echo "LOG_DIR=$OUT"
exit "$RC"
