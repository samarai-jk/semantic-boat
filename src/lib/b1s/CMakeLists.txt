# B1S, the CAN protocol of Semantic Boat devices (README.md). Firmware
# builds use the committed include/b1s/registry.hpp; regenerate it with
# tools/generate_registry.py after changing registry.yaml.
add_library(b1s STATIC
    src/channel_settings.cpp
    src/measurement_publisher.cpp
    src/quantity.cpp
    src/service_server.cpp
)

target_include_directories(b1s PUBLIC include)
target_compile_features(b1s PUBLIC cxx_std_17)
target_link_libraries(b1s PUBLIC n2k slstm32)
target_compile_options(b1s PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
