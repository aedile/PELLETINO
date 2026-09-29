#!/usr/bin/env bash
# Build and run the sound effect test: host/music/sfx.sh [dir-for-wavs]
set -euo pipefail
cd "$(dirname "$0")/../.."
C=components/chiptune
cc -O1 -w -Ihost/music/stub -I$C/include -I$C/src -fsanitize=address,undefined \
   -o /tmp/pelletino_test_sfx host/music/test_sfx.c $C/src/sfx.c -lm
exec /tmp/pelletino_test_sfx "$@"
