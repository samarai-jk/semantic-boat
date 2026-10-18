# can-mcu bootloader (src/lib/b1s/FIRMWARE.md). Register level, without HAL
# or CubeMX: it uses only the CMSIS device headers (taken from lab-test-01's
# CubeMX tree, the same STM32L431) and header-only parts of n2k and b1s.
set(CAN_MCU_BOOTLOADER_TARGET ${CMAKE_PROJECT_NAME})
add_executable(${CAN_MCU_BOOTLOADER_TARGET}
    src/hardware.cpp
    src/main.cpp
    src/node.cpp
    src/startup.cpp
)
set_target_properties(${CAN_MCU_BOOTLOADER_TARGET} PROPERTIES OUTPUT_NAME can-mcu-bootloader SUFFIX ".elf")

set(CAN_MCU_CMSIS ${CMAKE_CURRENT_LIST_DIR}/../lab-test-01/cmx/Drivers/CMSIS)
target_include_directories(${CAN_MCU_BOOTLOADER_TARGET} PRIVATE
    src
    ../common/include
    ../../../lib/b1s/include
    ../../../lib/n2k/include
    ${CAN_MCU_CMSIS}/Device/ST/STM32L4xx/Include
    ${CAN_MCU_CMSIS}/Include
)
target_compile_definitions(${CAN_MCU_BOOTLOADER_TARGET} PRIVATE STM32L431xx)
target_compile_features(${CAN_MCU_BOOTLOADER_TARGET} PRIVATE cxx_std_17)
target_compile_options(${CAN_MCU_BOOTLOADER_TARGET} PRIVATE -Wextra -Wpedantic)
target_link_options(${CAN_MCU_BOOTLOADER_TARGET} PRIVATE
    -Wl,-Map=$<TARGET_FILE_DIR:${CAN_MCU_BOOTLOADER_TARGET}>/can-mcu-bootloader.map)

# A product firmware without development tools (AGENTS.md).
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/firmware-profile.cmake)
sb_firmware_profile(${CAN_MCU_BOOTLOADER_TARGET} PRODUCTION)

add_custom_command(TARGET ${CAN_MCU_BOOTLOADER_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CAN_MCU_BOOTLOADER_TARGET}> $<TARGET_FILE_DIR:${CAN_MCU_BOOTLOADER_TARGET}>/can-mcu-bootloader.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CAN_MCU_BOOTLOADER_TARGET}> $<TARGET_FILE_DIR:${CAN_MCU_BOOTLOADER_TARGET}>/can-mcu-bootloader.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CAN_MCU_BOOTLOADER_TARGET}>
    VERBATIM
)
