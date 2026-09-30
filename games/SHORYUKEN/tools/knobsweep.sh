#!/bin/sh
# SPDX-License-Identifier: 0BSD
# What each of the other knobs buys: one bench per line below, all SCALE, FRAME_SKIP=3,
# FRAME_SKIP_AUTO=0, and the "bench" line of each appended to knobs.txt.
#
#   tools/knobsweep.sh <port>
set -e
cd "$(dirname "$0")/.."
PORT="$1"
OUT=knobs.txt
: > "$OUT"
while read -r label knobs; do
    [ -n "$label" ] || continue
    line=$(tools/bench.sh "$PORT" FRAME_SKIP_AUTO=0 $knobs | grep '^bench fps' || echo "bench failed")
    echo "$label $line" | tee -a "$OUT"
done <<'LIST'
defaults
idle_skip_off         IDLE_SKIP=0
hot_handlers_off      HOT_HANDLERS=0
opcode_table_ram      OPCODE_TABLE_RAM=1
opcode_table_ram_cold OPCODE_TABLE_RAM=1 HOT_HANDLERS=0
prog_cache_32         PROG_CACHE_KB=32 HOT_HANDLERS=0
occlusion_on          OCCLUSION=1
strip_reuse_off       STRIP_REUSE=0
rowscroll_off         ROWSCROLL=0
tiles_from_ram        EXPERIMENT=EXPERIMENT_RAM_TILES
layers_none           LAYERS=0
layers_scroll1        LAYERS=1
layers_scroll2        LAYERS=2
layers_scroll3        LAYERS=4
layers_sprites        LAYERS=8
sound                 SOUND=FM_ADPCM
sound_idle_skip_off   SOUND=FM_ADPCM IDLE_SKIP=0
sound_ym_quality_0    SOUND=FM_ADPCM YM_QUALITY=0
stats_off             STATS=0
LIST
