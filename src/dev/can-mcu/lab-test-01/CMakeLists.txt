if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(lab_test_01 LANGUAGES C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(../../../lib/slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
    add_subdirectory(../../../lib/n2k ${CMAKE_CURRENT_BINARY_DIR}/n2k)
    add_subdirectory(../../../lib/b1s ${CMAKE_CURRENT_BINARY_DIR}/b1s)
    add_subdirectory(../../../lib/semantic-modules ${CMAKE_CURRENT_BINARY_DIR}/semantic-modules)
endif()

set(LAB_TEST_01_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${LAB_TEST_01_TARGET})
set_target_properties(${LAB_TEST_01_TARGET} PROPERTIES OUTPUT_NAME lab-test-01 SUFFIX ".elf")

# CubeMX exports the HAL/CMSIS sources, include paths, and device definitions.
add_subdirectory(cmx/cmake/stm32cubemx ${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx)

include(../common/can-mcu.cmake)
can_mcu_add_common(${LAB_TEST_01_TARGET})

# Product and service code. Development and test code goes into src/dev/ and
# is listed only under DEV_SOURCES below (AGENTS.md).
target_sources(${LAB_TEST_01_TARGET} PRIVATE
    src/app.cpp
    src/board.cpp
    src/device_settings.cpp
    src/diagnostics_service.cpp
)

# LT1 is a lab device: development tools are on by default. Check that it also
# builds as a product with -DSB_DEV_TOOLS=OFF.
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/firmware-profile.cmake)
sb_firmware_profile(${LAB_TEST_01_TARGET} LAB
    DEV_SOURCES
        src/dev/activity_service.cpp
        src/dev/dev_tools.cpp
        src/dev/hardware_test_service.cpp
        src/dev/input_module_test_service.cpp
    STUB_SOURCES
        src/dev_tools_disabled.cpp
)
target_include_directories(${LAB_TEST_01_TARGET} PRIVATE include src)
target_link_libraries(${LAB_TEST_01_TARGET} b1s n2k semantic_modules slstm32 stm32cubemx)
target_link_options(${LAB_TEST_01_TARGET} PRIVATE
    -Wl,-Map=$<TARGET_FILE_DIR:${LAB_TEST_01_TARGET}>/lab-test-01.map)

# Started by the can-mcu bootloader: stamped image (lab-test-01.bin).
can_mcu_add_image(${LAB_TEST_01_TARGET} lab-test-01)
