idf_component_register(
    SRCS 
        "src/main.cpp"
        "src/fiesta_video.c"
        "src/game_state.c"
    INCLUDE_DIRS 
        "include"
        "roms"
        "../movie"
        "${IDF_PATH}/components/esp_rom/include/esp32c6/rom"
    REQUIRES
        driver
        esp_timer
        esp_rom
        audio_hal
        display
        z80_cpu
        pacman_hw
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_phy
        bt
)

# Which game to build: -DGAME=pacman (default, the Fiesta medal) or -DGAME=mspacman.
# ROM headers for the chosen game must exist in main/roms (tools/convert_roms.py).
if(NOT DEFINED GAME)
    set(GAME "pacman")
endif()
if(GAME STREQUAL "mspacman")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GAME_MSPACMAN=1)
    message(STATUS "PELLETINO: building Ms. Pac-Man")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GAME_PACMAN=1)
    message(STATUS "PELLETINO: building Pac-Man")
endif()
