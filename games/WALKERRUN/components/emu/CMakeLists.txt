# Platform-independent Star Wars / Empire Strikes Back emulation core (shared with the host harness in ../../host)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
idf_component_register(
    SRCS "${CORE_DIR}/avg.c" "${CORE_DIR}/starwars.c" "${CORE_DIR}/sound.c" "${CORE_DIR}/pokey.c" "${CORE_DIR}/tms5220.c" "${CORE_DIR}/autoplay.c" "${CORE_DIR}/slapstic.c"
    INCLUDE_DIRS "${CORE_DIR}"
    LDFRAGMENTS "linker.lf"
)
# ESP-IDF disables jump tables on RISC-V by default; the 6809 opcode dispatch
# is a 256-way switch and is ~10x slower as a compare chain.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion)
