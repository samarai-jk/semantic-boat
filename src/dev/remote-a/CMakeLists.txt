if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(remote_a LANGUAGES C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)
    add_subdirectory(../../lib/slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
endif()

set(REMOTE_A_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${REMOTE_A_TARGET})
set_target_properties(${REMOTE_A_TARGET} PROPERTIES OUTPUT_NAME remote-a SUFFIX ".elf")
option(REMOTE_A_USE_STLINK_DATA_LINK "Use the ST-Link virtual COM port for display data" ON)

# CubeMX exports the HAL/CMSIS sources, include paths, and device definitions.
add_subdirectory(cmx/cmake/stm32cubemx ${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx)

target_sources(${REMOTE_A_TARGET} PRIVATE
    src/alarm_service.cpp
    src/app.cpp
    src/board.cpp
    src/config_staging.cpp
    src/configuration_service.cpp
    src/device_settings.cpp
    src/device_shutdown_service.cpp
    src/device_metrics_service.cpp
    src/display_config.cpp
    src/display_input_service.cpp
    src/display_link_service.cpp
    src/feedback_service.cpp
    src/hal_eeprom.cpp
    src/hal_epd_transport.cpp
    src/setup_service.cpp
    src/uart_transport.cpp
)
target_include_directories(${REMOTE_A_TARGET} PRIVATE include src)
target_compile_definitions(${REMOTE_A_TARGET} PRIVATE
    REMOTE_A_USE_STLINK_DATA_LINK=$<BOOL:${REMOTE_A_USE_STLINK_DATA_LINK}>)
target_link_libraries(${REMOTE_A_TARGET} semantic_display semantic_link slstm32 stm32cubemx)
target_link_options(${REMOTE_A_TARGET} PRIVATE -Wl,-Map=$<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.map)

add_custom_command(TARGET ${REMOTE_A_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${REMOTE_A_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${REMOTE_A_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${REMOTE_A_TARGET}>
    VERBATIM
)
