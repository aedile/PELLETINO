#!/bin/sh
# SPDX-License-Identifier: 0BSD
# Build one configuration in Docker, flash the application, run the bench, print the log.
#
#   tools/bench.sh <port> [knob=value ...]
#   tools/bench.sh /dev/cu.usbmodem1101 VIDEO_MODE=CROP FRAME_SKIP=1 SOUND=FM SOUND_RATE=22050
#
# Every knob that is not named is put back to its default, so one run does not inherit the
# last one's settings from the CMake cache. BENCH_SECONDS defaults to 25 here: the first
# attract fight. roms.bin is not flashed: do that once (see the README).
#
# Exit status: 0 the bench ran; 1 it did not finish in time; 2 the board could not be
# written; 3 the program could not be started (see capture.py).
set -e
cd "$(dirname "$0")/.."
PORT="$1"; shift
DEFS="-DVIDEO_MODE=SCALE -DFRAME_SKIP=3 -DFRAME_SKIP_AUTO=1 -DLAYERS=15 -DROWSCROLL=1 -DSOUND=OFF -DSOUND_RATE=22050 -DYM_QUALITY=1 -DCPU_CORE=MUSASHI -DIDLE_SKIP=1 -DPROG_CACHE_KB=0 -DHOT_HANDLERS=1 -DOPCODE_TABLE_RAM=0 -DOCCLUSION=0 -DSTRIP_REUSE=1 -DTILE_CACHE_KB=0 -DSTATS=1 -DBENCH_SECONDS=25 -DBENCH_FROM_FRAME=3400 -DEXPERIMENT="
for kv in "$@"; do DEFS="$DEFS -D$kv"; done
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 $DEFS build > build_docker.log 2>&1 \
    || { tail -40 build_docker.log; echo "build failed: see build_docker.log" >&2; exit 1; }
ESPTOOL=$(command -v esptool.py || command -v esptool)
PY=$(head -1 "$ESPTOOL" | sed 's/^#!//')
# esptool writes the application and leaves the chip in download mode; capture.py starts it
# (opening the port is what resets it) and listens
"$ESPTOOL" --chip esp32c6 -p "$PORT" -b 460800 --after no_reset write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build_docker/shoryuken.bin > /dev/null \
    || { echo "bench: could not write to the board" >&2; exit 2; }
SECS=$(echo "$DEFS" | sed 's/.*-DBENCH_SECONDS=\([0-9]*\).*/\1/')
"$PY" tools/capture.py "$PORT" $((SECS + 90))
