idf_component_register(
    SRCS "main.cpp" "egg.cpp" "render.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_partition display audio_hal medal_input medalboot helix_mp3 imu
)
