# music/credits.txt names whoever wrote the music you supplied; it is embedded when present
set(CREDITS_TXT "${CMAKE_CURRENT_LIST_DIR}/../music/credits.txt")
set(TXT "")
if(EXISTS ${CREDITS_TXT})
    set(TXT ${CREDITS_TXT})
endif()

idf_component_register(
    SRCS "main.cpp" "menu.cpp" "input.cpp" "games.cpp" "splash.c" "credits.c" "howto.c" "battery.c" "sound.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_partition app_update nvs_flash display imu mqart medalboot fest chiptune audio_hal esp_adc
    EMBED_TXTFILES ${TXT}
)

if(EXISTS ${CREDITS_TXT})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_MUSIC_CREDITS=1)
endif()

# a launcher that exercises itself and reports over serial; see selftest_tick() in main.cpp
if(DEFINED ENV{PELLETINO_SELFTEST})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PELLETINO_SELFTEST=1)
endif()

# a launcher that boots the next game each time it starts; see tour() in main.cpp
if(DEFINED ENV{PELLETINO_TOUR})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PELLETINO_TOUR=1)
    if(NOT "$ENV{PELLETINO_TOUR}" STREQUAL "1")      # PELLETINO_TOUR=polepos boots that game every time
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PELLETINO_TOUR_ONLY="$ENV{PELLETINO_TOUR}")
    endif()
endif()
