if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(n2k LANGUAGES CXX)
    add_subdirectory(../slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
endif()

add_library(n2k STATIC
    src/node.cpp
)

target_include_directories(n2k PUBLIC include)
target_compile_features(n2k PUBLIC cxx_std_17)
target_link_libraries(n2k PUBLIC slstm32)
target_compile_options(n2k PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
