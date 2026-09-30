# Platform-independent CPS-1 core (shared with the host harness in ../../host)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
include(${CMAKE_CURRENT_LIST_DIR}/../../knobs.cmake)
idf_component_register(
    SRCS "${CORE_DIR}/cps1.c" "${CORE_DIR}/cps1_video.c" "${CORE_DIR}/cps1_sound.c"
         "${CORE_DIR}/cps1_idle.c" "${CORE_DIR}/ym2151.c" "${CORE_DIR}/okim6295.c" "${CORE_DIR}/m68kown.c"
         "${CORE_DIR}/m68k/m68kcpu.c" "${CORE_DIR}/m68k/m68kops.c"
         "${CORE_DIR}/z80/Z80.c"
    INCLUDE_DIRS "${CORE_DIR}" "${CORE_DIR}/m68k" "${CORE_DIR}/z80" "${CMAKE_CURRENT_SOURCE_DIR}/../../main"
    LDFRAGMENTS "linker.lf"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion
    -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-implicit-fallthrough)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LSB_FIRST EXECZ80 ${SHORYUKEN_KNOB_DEFS})
