# Board support shared by all can-mcu firmware. The sources include the
# device's CubeMX headers (main.h), so they are compiled into each device
# target rather than built as a separate library.
set(CAN_MCU_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

function(can_mcu_add_common target)
    target_sources(${target} PRIVATE
        ${CAN_MCU_COMMON_DIR}/src/board.cpp
        ${CAN_MCU_COMMON_DIR}/src/board_temperatures.cpp
        ${CAN_MCU_COMMON_DIR}/src/can_bus.cpp
        ${CAN_MCU_COMMON_DIR}/src/interrupts.cpp
        ${CAN_MCU_COMMON_DIR}/src/i2c_bus.cpp
        ${CAN_MCU_COMMON_DIR}/src/mcu_monitor.cpp
        ${CAN_MCU_COMMON_DIR}/src/supervision.cpp
    )
    target_include_directories(${target} PRIVATE ${CAN_MCU_COMMON_DIR}/include)
endfunction()
