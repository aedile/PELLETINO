idf_component_register(
    SRCS
        "src/z80_cpu.c"
        "src/Z80.c"
    INCLUDE_DIRS
        "include"
        "src"
    REQUIRES
        driver
)

# Z80 emulator needs LSB_FIRST for ESP32 (little-endian)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LSB_FIRST)
