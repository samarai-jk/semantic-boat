if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(semantic_modules LANGUAGES CXX)
    add_subdirectory(../slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
endif()

add_library(semantic_modules STATIC
    src/ad_input_iso_12v.cpp
)

target_include_directories(semantic_modules PUBLIC include)
target_compile_features(semantic_modules PUBLIC cxx_std_17)
target_link_libraries(semantic_modules PUBLIC slstm32)
target_compile_options(semantic_modules PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
