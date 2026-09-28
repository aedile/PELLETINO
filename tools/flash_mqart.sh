#!/usr/bin/env bash
# Flash the marquee blob into the mqart partition.
#
# The app and the artwork are flashed separately on purpose: re-running
# pack_art.py and this script updates every marquee without rebuilding or
# reflashing the launcher.
#
# Run from the host, not the build container - Docker Desktop on macOS cannot
# reach USB devices.
set -euo pipefail
cd "$(dirname "$0")/.."

BLOB=lcd/marquees.bin
PORT="${1:-}"

[ -f "$BLOB" ] || { echo "no $BLOB - run tools/pack_art.py first" >&2; exit 1; }

# Take the offset and size straight from the partition table so they cannot drift.
read -r OFFSET SIZE < <(awk -F',' '/^mqart/ {gsub(/ /,"",$4); gsub(/ /,"",$5); print $4, $5}' partitions.csv)
[ -n "${OFFSET:-}" ] || { echo "no mqart row in partitions.csv" >&2; exit 1; }

BLOB_SZ=$(stat -f%z "$BLOB" 2>/dev/null || stat -c%s "$BLOB")
if [ "$BLOB_SZ" -gt "$((SIZE))" ]; then
    echo "blob is $BLOB_SZ bytes but the mqart partition is $((SIZE)) - enlarge it in partitions.csv" >&2
    exit 1
fi

ESPTOOL=$(command -v esptool.py || echo "python3 -m esptool")
echo "flashing $BLOB ($((BLOB_SZ/1024)) KB) to mqart at $OFFSET"
$ESPTOOL --chip esp32c6 ${PORT:+-p "$PORT"} write_flash "$OFFSET" "$BLOB"
