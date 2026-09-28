#!/usr/bin/env bash
# Build and run the music test: host/music/run.sh <file> [track] [out.wav]
set -euo pipefail
cd "$(dirname "$0")/../.."
C=components/chiptune
cc -O1 -w -Ihost/music/stub -I$C/include -I$C/src -o /tmp/pelletino_test_music \
   host/music/test_music.c $C/src/chiptune.c $C/src/nsf.c $C/src/apu2a03.c \
   $C/src/midi_ay.c $C/src/ay8910.c
exec /tmp/pelletino_test_music "$@"
