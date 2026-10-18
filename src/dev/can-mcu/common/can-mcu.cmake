# Board support shared by all can-mcu firmware. The sources include the
# device's CubeMX headers (main.h), so they are compiled into each device
# target rather than built as a separate library.
set(CAN_MCU_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

function(can_mcu_add_common target)
    target_sources(${target} PRIVATE
        ${CAN_MCU_COMMON_DIR}/src/board.cpp
        ${CAN_MCU_COMMON_DIR}/src/board_temperatures.cpp
        ${CAN_MCU_COMMON_DIR}/src/can_bus.cpp
        ${CAN_MCU_COMMON_DIR}/src/dac_audio.cpp
        ${CAN_MCU_COMMON_DIR}/src/firmware.cpp
        ${CAN_MCU_COMMON_DIR}/src/interrupts.cpp
        ${CAN_MCU_COMMON_DIR}/src/i2c_bus.cpp
        ${CAN_MCU_COMMON_DIR}/src/mcu_monitor.cpp
        ${CAN_MCU_COMMON_DIR}/src/one_wire_pin.cpp
        ${CAN_MCU_COMMON_DIR}/src/supervision.cpp
    )
    target_include_directories(${target} PRIVATE ${CAN_MCU_COMMON_DIR}/include)
endfunction()

# Application image for the can-mcu bootloader (src/lib/b1s/FIRMWARE.md):
# stamps size and CRC into the image header of <target>.elf and writes
# <name>.bin (the image sent over CAN) and <name>.hex. Needs Python 3.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(CAN_MCU_STAMP_IMAGE ${CAN_MCU_COMMON_DIR}/../../../lib/b1s/tools/stamp_image.py)

function(can_mcu_add_image target name)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CAN_MCU_STAMP_IMAGE} --objcopy ${CMAKE_OBJCOPY}
            $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${name}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${name}.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM
    )
endfunction()
