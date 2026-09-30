#!/bin/sh
# SPDX-License-Identifier: 0BSD
# Does this build still emulate the same machine? Runs the harness with the sound off and
# with it on and compares a hash of every frame (the picture, graphics RAM, the frame's
# sound) and the whole of the sound with reference logs made by a build that is trusted.
#
#   host/check.sh <roms.bin> <reference directory> [make]     "make" writes the reference
#
# The reference is made with IDLE_SKIP=0 and YM_COMPUTE_ALL, so a pass also says that
# skipping the idle loops and the silent FM channels changes nothing. 300 seconds: five times round the attract mode.
set -e
abs() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
ROMS=$(abs "$1"); REF=$(abs "$2"); MODE="$3"; SECS=${SECS:-300}
cd "$(dirname "$0")"
T=$(mktemp -d)
if [ "$MODE" = make ]; then K="-DIDLE_SKIP=0 -DYM_COMPUTE_ALL"; OUT="$REF"; mkdir -p "$REF"; else K="$KNOBS"; OUT="$T"; fi
make -B -s KNOBS="$K" >/dev/null
HASH_LOG="$OUT/video.txt" ./harness "$ROMS" "$T" $SECS --view native --every 100000 --quiet >/dev/null
make -B -s KNOBS="$K -DKNOB_SOUND=FM_ADPCM" >/dev/null
HASH_LOG="$OUT/sound.txt" ./harness "$ROMS" "$T" $SECS --view scale --every 100000 --quiet --wav "$OUT/sound.wav" >/dev/null
make -B -s KNOBS="$KNOBS" >/dev/null
if [ "$MODE" = make ]; then echo "reference written to $REF"; rm -rf "$T"; exit 0; fi
ok=1
for f in video.txt sound.txt sound.wav; do
    if cmp -s "$T/$f" "$REF/$f"; then echo "  $f: identical"; else echo "  $f: DIFFERS"; ok=0; fi
done
rm -rf "$T"
[ $ok = 1 ] && echo "same machine" || { echo "NOT the same machine"; exit 1; }
