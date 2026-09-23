#!/bin/sh
set -u
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" != I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER ]; then
 echo 'Usage: run-coffee-demo.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [media-store] [ticks]' >&2
 exit 2
fi
STORE=${2:-/tmp/coffee-media}
TICKS=${3:-3600}
STAMP=$(date +%Y%m%d-%H%M%S)-$$
OUT="$BASE/logs/coffee-$STAMP"
mkdir -p "$OUT"
(
 echo "COFFEE_DEMO_BEGIN store=$STORE ticks=$TICKS"
 echo 'Run only while idle; pause original UI/input consumer. Keep machine control/network/OTA running.'
 echo 'Simulation only. No heater/dispense command. USB is never mounted or executed by this program.'
 uname -a
 sha256sum "$BASE/ui-host-imx6ul-static" "$BASE/mediactl"
 "$BASE/ui-host-imx6ul-static" --touch-test --app coffee-demo --profile imx6ul-1024x600 \
  --fbdev /dev/fb0 --input-dir /dev/input --asset-root "$BASE/assets" --media-store "$STORE" \
  --ticks "$TICKS" --output "$OUT/coffee.json" --trace-output "$OUT/input-guest-present.csv"
 RC=$?
 echo "COFFEE_EXIT_CODE=$RC"
 [ ! -f "$OUT/coffee.json" ] || cat "$OUT/coffee.json"
 exit "$RC"
) >"$OUT/startup.log" 2>&1
RC=$?
cat "$OUT/startup.log"
(cd "$OUT" && sha256sum startup.log coffee.json input-guest-present.csv >SHA256SUMS) || true
echo "LOG_DIR=$OUT"
exit "$RC"
