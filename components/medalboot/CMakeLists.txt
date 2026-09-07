idf_component_register(
    SRCS "src/medalboot.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash app_update esp_partition esp_timer
)
