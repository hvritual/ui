#!/bin/sh
# Isolated 600-profile development verification; does not stop other services.
set -eu
cd "$(dirname "$0")"
duration=${1:-60}
output=${2:-physical-600}
case "$duration" in ''|*[!0-9]*) echo 'invalid duration' >&2; exit 2;; esac
[ "$duration" -ge 5 ] && [ "$duration" -le 3600 ]
case "$output" in ''|*[!A-Za-z0-9_.-]*) echo 'output must be a new simple directory name' >&2; exit 2;; esac
[ ! -e "$output" ] && [ ! -e "$output.log" ]
[ "$(cat /sys/class/graphics/fb0/virtual_size)" = '1024,600' ]
[ "$(cat /sys/class/graphics/fb0/bits_per_pixel)" = 32 ]
sha256sum -c SHA256SUMS
./ui-framework --physical --profile imx6ul-1024x600 --package trend-chart.pui \
  --allow-unsigned-package --seconds "$duration" --output "$output" > "$output.log" 2>&1
# Read the actual display memory after the final present, using observed stride.
stride=$(cat /sys/class/graphics/fb0/stride)
case "$stride" in ''|*[!0-9]*) echo 'invalid stride' >&2; exit 2;; esac
dd if=/dev/fb0 of="$output/framebuffer.raw" bs="$stride" count=600
cp "$output.log" "$output/runtime.log"
echo "TREND_PHYSICAL_RETURNED output=$output; verify report, source and framebuffer before admission"
