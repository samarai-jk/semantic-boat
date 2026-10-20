if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(engine_mon LANGUAGES C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(../../../lib/slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
    add_subdirectory(../../../lib/n2k ${CMAKE_CURRENT_BINARY_DIR}/n2k)
    add_subdirectory(../../../lib/b1s ${CMAKE_CURRENT_BINARY_DIR}/b1s)
    add_subdirectory(../../../lib/semantic-modules ${CMAKE_CURRENT_BINARY_DIR}/semantic-modules)
endif()

set(ENGINE_MON_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${ENGINE_MON_TARGET})
set_target_properties(${ENGINE_MON_TARGET} PROPERTIES OUTPUT_NAME engine-mon SUFFIX ".elf")

# CubeMX exports the HAL/CMSIS sources, include paths, and device definitions.
add_subdirectory(cmx/cmake/stm32cubemx ${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx)

include(../common/can-mcu.cmake)
can_mcu_add_common(${ENGINE_MON_TARGET})

# Product and service code. Development and test code goes into src/dev/ and
# is listed only under DEV_SOURCES below (AGENTS.md).
target_sources(${ENGINE_MON_TARGET} PRIVATE
    src/app.cpp
    src/board.cpp
    src/device_settings.cpp
    src/io_console.cpp
)

# engine-mon is a product: development tools are off by default; a Debug
# build can have them with -DSB_DEV_TOOLS=ON, a Release build never.
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/firmware-profile.cmake)
sb_firmware_profile(${ENGINE_MON_TARGET} PRODUCTION
    DEV_SOURCES
        src/dev/dev_tools.cpp
    STUB_SOURCES
        src/dev_tools_disabled.cpp
)
target_include_directories(${ENGINE_MON_TARGET} PRIVATE include src)
target_link_libraries(${ENGINE_MON_TARGET} b1s n2k semantic_modules slstm32 stm32cubemx)
target_link_options(${ENGINE_MON_TARGET} PRIVATE
    -Wl,-Map=$<TARGET_FILE_DIR:${ENGINE_MON_TARGET}>/engine-mon.map)

# Started by the can-mcu bootloader: stamped image (engine-mon.bin).
can_mcu_add_image(${ENGINE_MON_TARGET} engine-mon)
