idf_component_register(
    SRCS "main.cpp" "input.cpp" "render.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer display emu audio_hal medal_input medalboot app_update esp_partition
)
