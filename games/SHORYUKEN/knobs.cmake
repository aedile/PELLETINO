# SPDX-License-Identifier: 0BSD
# Turns -D<KNOB>=<value> on the idf.py command line into knobs_build.h in the build
# directory, which main/knobs.h includes. Included by the emu component, which puts the
# build directory on the include path of everything that uses it.
#
# A header rather than compiler flags, so that changing a knob recompiles the files that
# read knobs.h and not the 68000, which is a megabyte of source and does not.
#
#   idf.py -B build_docker -DVIDEO_MODE=CROP -DFRAME_SKIP=1 -DSOUND=FM -DSOUND_RATE=22050 build
#
# CMake keeps a -D value in the build directory's cache until it is given again or the
# directory is deleted, so a sweep names every knob on every build.
set(SHORYUKEN_KNOB_DEFS "")
foreach(knob FRAME_SKIP FRAME_SKIP_AUTO LAYERS ROWSCROLL SOUND_RATE YM_QUALITY TILE_CACHE_KB
             STATS BENCH_SECONDS BENCH_FROM_FRAME IDLE_SKIP
             PROG_CACHE_KB HOT_HANDLERS OPCODE_TABLE_RAM OCCLUSION STRIP_REUSE)
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

set(SHORYUKEN_KNOB_TEXT "/* written by knobs.cmake from the idf.py command line */\n")
foreach(def ${SHORYUKEN_KNOB_DEFS})
    string(REPLACE "=" " " def "${def}")
    string(APPEND SHORYUKEN_KNOB_TEXT "#define ${def}\n")
endforeach()
# the two the 68000 reads get a header of their own, so that changing any other knob does
# not recompile it
set(SHORYUKEN_M68K_TEXT "/* written by knobs.cmake: the knobs the 68000 reads */\n")
foreach(knob PROG_CACHE_KB OPCODE_TABLE_RAM)
    if(DEFINED ${knob})
        string(APPEND SHORYUKEN_M68K_TEXT "#define ${knob} ${${knob}}\n")
    endif()
endforeach()

# a header is only written when what is in it changes, so that make sees no change
function(shoryuken_write_if_changed path text)
    set(old "")
    if(EXISTS "${path}")
        file(READ "${path}" old)
    endif()
    if(NOT "${old}" STREQUAL "${text}")
        file(WRITE "${path}" "${text}")
    endif()
endfunction()
shoryuken_write_if_changed("${CMAKE_BINARY_DIR}/knobs/knobs_build.h" "${SHORYUKEN_KNOB_TEXT}")
shoryuken_write_if_changed("${CMAKE_BINARY_DIR}/knobs/knobs_m68k_build.h" "${SHORYUKEN_M68K_TEXT}")
