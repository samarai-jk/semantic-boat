if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(slstm32 LANGUAGES CXX)
endif()

add_library(slstm32 STATIC
    src/bare_executor.cpp
    src/event_bus.cpp
    src/drivers/analog_input.cpp
    src/drivers/at24c256.cpp
    src/drivers/button.cpp
    src/drivers/digital_io.cpp
    src/drivers/latching_relay.cpp
    src/drivers/led.cpp
    src/drivers/pwm_output.cpp
    src/drivers/rgb_led.cpp
    src/drivers/tone.cpp
    src/epd/font5x7.cpp
    src/epd/monochrome_canvas.cpp
    src/epd/waveshare_3in7.cpp
    src/storage/redundant_blob_store.cpp
)

target_include_directories(slstm32 PUBLIC include)
target_compile_features(slstm32 PUBLIC cxx_std_17)
target_compile_options(slstm32 PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
