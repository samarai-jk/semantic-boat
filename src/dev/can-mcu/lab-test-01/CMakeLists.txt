if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(lab_test_01 LANGUAGES C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(../../../lib/slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
    add_subdirectory(../../../lib/n2k ${CMAKE_CURRENT_BINARY_DIR}/n2k)
    add_subdirectory(../../../lib/semantic-modules ${CMAKE_CURRENT_BINARY_DIR}/semantic-modules)
endif()

set(LAB_TEST_01_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${LAB_TEST_01_TARGET})
set_target_properties(${LAB_TEST_01_TARGET} PROPERTIES OUTPUT_NAME lab-test-01 SUFFIX ".elf")

# CubeMX exports the HAL/CMSIS sources, include paths, and device definitions.
add_subdirectory(cmx/cmake/stm32cubemx ${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx)

include(../common/can-mcu.cmake)
can_mcu_add_common(${LAB_TEST_01_TARGET})

target_sources(${LAB_TEST_01_TARGET} PRIVATE
    src/app.cpp
    src/board.cpp
    src/can_monitor_service.cpp
    src/device_settings.cpp
    src/diagnostics_service.cpp
    src/hardware_test_service.cpp
    src/i2c_scan_service.cpp
    src/input_module_test_service.cpp
    src/temperature_report_service.cpp
)
target_include_directories(${LAB_TEST_01_TARGET} PRIVATE include src)
target_link_libraries(${LAB_TEST_01_TARGET} n2k semantic_modules slstm32 stm32cubemx)
target_link_options(${LAB_TEST_01_TARGET} PRIVATE
    -Wl,-Map=$<TARGET_FILE_DIR:${LAB_TEST_01_TARGET}>/lab-test-01.map)

add_custom_command(TARGET ${LAB_TEST_01_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${LAB_TEST_01_TARGET}> $<TARGET_FILE_DIR:${LAB_TEST_01_TARGET}>/lab-test-01.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${LAB_TEST_01_TARGET}> $<TARGET_FILE_DIR:${LAB_TEST_01_TARGET}>/lab-test-01.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${LAB_TEST_01_TARGET}>
    VERBATIM
)
