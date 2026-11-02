# remote-a bootloader (README.md). Register level, without HAL or CubeMX: it
# uses only the CMSIS device headers from remote-a's CubeMX tree, the
# semantic-link protocol, the B1S image format, and remote-a's settings
# record (the same code the application reads it with).
set(REMOTE_A_BOOTLOADER_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${REMOTE_A_BOOTLOADER_TARGET}
    src/hardware.cpp
    src/link.cpp
    src/main.cpp
    src/startup.cpp
    ../remote-a/src/device_settings.cpp
    ../../lib/semantic-link/src/protocol.cpp
    ../../lib/slstm32/src/storage/redundant_blob_store.cpp
)
set_target_properties(${REMOTE_A_BOOTLOADER_TARGET} PROPERTIES OUTPUT_NAME remote-a-bootloader SUFFIX ".elf")

set(REMOTE_A_CMSIS ${CMAKE_CURRENT_LIST_DIR}/../remote-a/cmx/Drivers/CMSIS)
target_include_directories(${REMOTE_A_BOOTLOADER_TARGET} PRIVATE
    src
    ../remote-a/include
    ../remote-a/src
    ../../lib/b1s/include
    ../../lib/semantic-link/include
    ../../lib/semantic-display/include
    ../../lib/slstm32/include
    ${REMOTE_A_CMSIS}/Device/ST/STM32L4xx/Include
    ${REMOTE_A_CMSIS}/Include
)
target_compile_definitions(${REMOTE_A_BOOTLOADER_TARGET} PRIVATE STM32L431xx)
target_compile_features(${REMOTE_A_BOOTLOADER_TARGET} PRIVATE cxx_std_17)
target_compile_options(${REMOTE_A_BOOTLOADER_TARGET} PRIVATE -Wextra -Wpedantic)
target_link_options(${REMOTE_A_BOOTLOADER_TARGET} PRIVATE
    -Wl,-Map=$<TARGET_FILE_DIR:${REMOTE_A_BOOTLOADER_TARGET}>/remote-a-bootloader.map)

# A product firmware without development tools (AGENTS.md).
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/firmware-profile.cmake)
sb_firmware_profile(${REMOTE_A_BOOTLOADER_TARGET} PRODUCTION)

add_custom_command(TARGET ${REMOTE_A_BOOTLOADER_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${REMOTE_A_BOOTLOADER_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_BOOTLOADER_TARGET}>/remote-a-bootloader.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${REMOTE_A_BOOTLOADER_TARGET}> $<TARGET_FILE_DIR:${REMOTE_A_BOOTLOADER_TARGET}>/remote-a-bootloader.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${REMOTE_A_BOOTLOADER_TARGET}>
    VERBATIM
)
