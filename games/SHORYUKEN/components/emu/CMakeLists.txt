# Platform-independent CPS-1 core (shared with the host harness in ../../host)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
include(${CMAKE_CURRENT_LIST_DIR}/../../knobs.cmake)
# HOT_HANDLERS=0 leaves every one of the 68000's instruction handlers in flash
if(DEFINED HOT_HANDLERS AND NOT HOT_HANDLERS)
    set(EMU_LINKER_FRAGMENT "linker_cold.lf")
else()
    set(EMU_LINKER_FRAGMENT "linker.lf")
endif()
idf_component_register(
    SRCS "${CORE_DIR}/cps1.c" "${CORE_DIR}/cps1_video.c" "${CORE_DIR}/cps1_sound.c"
         "${CORE_DIR}/cps1_idle.c" "${CORE_DIR}/ym2151.c" "${CORE_DIR}/okim6295.c" "${CORE_DIR}/m68kown.c"
         "${CORE_DIR}/m68k/m68kcpu.c" "${CORE_DIR}/m68k/m68kops.c"
         "${CORE_DIR}/z80/Z80.c"
    INCLUDE_DIRS "${CORE_DIR}" "${CORE_DIR}/m68k" "${CORE_DIR}/z80" "${CMAKE_CURRENT_SOURCE_DIR}/../../main"
                 "${CMAKE_BINARY_DIR}/knobs"
    LDFRAGMENTS "${EMU_LINKER_FRAGMENT}"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion
    -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-implicit-fallthrough)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LSB_FIRST EXECZ80 ${EXPERIMENT})
if(EXPERIMENT MATCHES VIDEO_PROFILE)
    target_include_directories(${COMPONENT_LIB} PUBLIC $<TARGET_PROPERTY:__idf_esp_hw_support,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
