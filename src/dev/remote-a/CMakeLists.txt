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

# CubeMX exports the HAL/CMSIS sources, include paths, and device definitions.
add_subdirectory(cmx/cmake/stm32cubemx ${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx)

target_sources(${REMOTE_A_TARGET} PRIVATE
    src/app.cpp
    src/board.cpp
    src/feedback_service.cpp
    src/hal_epd_transport.cpp
    src/navigation_service.cpp
    src/rs485_bridge.cpp
)
target_include_directories(${REMOTE_A_TARGET} PRIVATE include src)
target_link_libraries(${REMOTE_A_TARGET} slstm32 stm32cubemx)
target_link_options(${REMOTE_A_TARGET} PRIVATE -Wl,-Map=$<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.map)

add_custom_command(TARGET ${REMOTE_A_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${REMOTE_A_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${REMOTE_A_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_TARGET}>/remote-a.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${REMOTE_A_TARGET}>
    VERBATIM
)
