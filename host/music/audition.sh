#!/usr/bin/env bash
# Render every track of an NSF to a WAV so you can find the one you want by ear.
#
#     host/music/audition.sh music/splash.nsf            # all tracks
#     host/music/audition.sh music/splash.nsf 12          # the first 12
#
# Thirty seconds each, written to /tmp/pelletino_audition/. Nothing leaves your machine.
set -euo pipefail
cd "$(dirname "$0")/../.."
FILE="${1:?usage: host/music/audition.sh <file.nsf> [how-many]}"
OUT=/tmp/pelletino_audition
mkdir -p "$OUT"
TOTAL=$(python3 -c "import sys; d=open(sys.argv[1],'rb').read(); print(d[6] if d[:5]==b'NESM\x1a' else 1)" "$FILE")
N="${2:-$TOTAL}"; [ "$N" -gt "$TOTAL" ] && N="$TOTAL"
NAME=$(basename "${FILE%.*}")
echo "  $FILE: $TOTAL tracks, rendering $N"
for t in $(seq 1 "$N"); do
    w="$OUT/${NAME}_$(printf %02d "$t").wav"
    if host/music/run.sh "$FILE" "$t" "$w" >/dev/null 2>&1; then s=ok; else s="quiet or short (a jingle or a sound effect)"; fi
    printf '  track %2d  %-44s afplay %s\n' "$t" "$s" "$w"
done
echo
echo "  when you have it:  echo <number> > music/${NAME}.track && ./pelletino build && ./pelletino flash"
