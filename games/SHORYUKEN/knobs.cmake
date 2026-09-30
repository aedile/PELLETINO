# SPDX-License-Identifier: 0BSD
# Turns -D<KNOB>=<value> on the idf.py command line into compile definitions for the
# components that include main/knobs.h. Included by main, emu and audio_hal.
#
#   idf.py -B build_docker -DVIDEO_MODE=CROP -DFRAME_SKIP=1 -DSOUND=FM -DSOUND_RATE=22050 build
#
# CMake keeps a -D value in the build directory's cache until it is given again or the
# directory is deleted, so a sweep names every knob on every build.
set(SHORYUKEN_KNOB_DEFS "")
foreach(knob FRAME_SKIP FRAME_SKIP_AUTO LAYERS ROWSCROLL SOUND_RATE YM_QUALITY TILE_CACHE_KB
             STATS BENCH_SECONDS BENCH_FROM_FRAME IDLE_SKIP)
    if(DEFINED ${knob})
        list(APPEND SHORYUKEN_KNOB_DEFS "${knob}=${${knob}}")
    endif()
endforeach()
# these take a word, which knobs.h pastes onto a prefix
foreach(knob VIDEO_MODE SOUND CPU_CORE)
    if(DEFINED ${knob})
        list(APPEND SHORYUKEN_KNOB_DEFS "KNOB_${knob}=${${knob}}")
    endif()
endforeach()
