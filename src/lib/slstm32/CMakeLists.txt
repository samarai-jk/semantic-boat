if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(slstm32 LANGUAGES CXX)
endif()

add_library(slstm32 STATIC
    src/bare_executor.cpp
    src/console.cpp
    src/event_bus.cpp
    src/log.cpp
    src/drivers/ads1115.cpp
    src/drivers/analog_input.cpp
    src/drivers/analog_sensor.cpp
    src/drivers/as3935.cpp
    src/drivers/at24c256.cpp
    src/drivers/bmp585.cpp
    src/drivers/button.cpp
    src/drivers/can_bus.cpp
    src/drivers/ct1780.cpp
    src/drivers/dfrobot_uv_index.cpp
    src/drivers/dht20.cpp
    src/drivers/digital_io.cpp
    src/drivers/ds18b20.cpp
    src/drivers/i2c_bus.cpp
    src/drivers/latching_relay.cpp
    src/drivers/led.cpp
    src/drivers/one_wire.cpp
    src/drivers/pwm_led.cpp
    src/drivers/pwm_output.cpp
    src/drivers/rgb_led.cpp
    src/drivers/serial_log.cpp
    src/drivers/sht31.cpp
    src/drivers/tmp75.cpp
    src/drivers/tone.cpp
    src/drivers/tone_pattern.cpp
    src/drivers/veml7700.cpp
    src/epd/font5x7.cpp
    src/epd/monochrome_canvas.cpp
    src/epd/waveshare_3in7.cpp
    src/sensors/alarm_annunciator.cpp
    src/sensors/measurement.cpp
    src/sensors/measurement_console.cpp
    src/sensors/measurement_log.cpp
    src/sensors/registry.cpp
    src/sensors/rtd.cpp
    src/storage/keyed_record_store.cpp
    src/storage/redundant_blob_store.cpp
)

target_include_directories(slstm32 PUBLIC include)
target_compile_features(slstm32 PUBLIC cxx_std_17)
target_compile_options(slstm32 PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
