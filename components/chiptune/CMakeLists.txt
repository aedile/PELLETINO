# Music is picked up from music/ when present. No file, no music, no build error.
# Each tune is copied into the build directory under a fixed name so the firmware
# does not care whether it started life as an .nsf or a .mid.
set(MUSIC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../music")
set(EMBEDS "")
set(TUNE_DEFS "")

foreach(slot splash credits)
    set(src "")
    foreach(ext nsf mid)
        if(NOT src AND EXISTS "${MUSIC_DIR}/${slot}.${ext}")
            set(src "${MUSIC_DIR}/${slot}.${ext}")
        endif()
    endforeach()
    string(TOUPPER ${slot} SLOT)
    if(src)
        set(dst "${CMAKE_BINARY_DIR}/music/tune_${slot}.bin")
        if(NOT CMAKE_BUILD_EARLY_EXPANSION)
            configure_file("${src}" "${dst}" COPYONLY)
            message(STATUS "chiptune: ${slot} music from ${src}")
        endif()
        list(APPEND EMBEDS "${dst}")
        list(APPEND TUNE_DEFS "HAVE_TUNE_${SLOT}=1")
        if(EXISTS "${MUSIC_DIR}/${slot}.track")
            file(READ "${MUSIC_DIR}/${slot}.track" track)
            string(STRIP "${track}" track)
            if(track MATCHES "^[0-9]+$")
                list(APPEND TUNE_DEFS "TUNE_${SLOT}_TRACK=${track}")
            endif()
        endif()
    elseif(NOT CMAKE_BUILD_EARLY_EXPANSION)
        message(STATUS "chiptune: no ${slot} music - that screen will be silent")
    endif()
endforeach()

idf_component_register(
    SRCS "src/chiptune.c" "src/sfx.c" "src/nsf.c" "src/apu2a03.c" "src/midi_ay.c" "src/ay8910.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES audio_hal
    EMBED_FILES ${EMBEDS}
)

if(TUNE_DEFS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${TUNE_DEFS})
endif()
