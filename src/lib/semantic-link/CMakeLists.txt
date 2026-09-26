if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(semantic_link LANGUAGES CXX)
endif()

add_library(semantic_link STATIC src/protocol.cpp)
target_include_directories(semantic_link PUBLIC include)
target_compile_features(semantic_link PUBLIC cxx_std_17)
target_compile_options(semantic_link PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(semantic_link_tests tests/protocol_tests.cpp)
        target_link_libraries(semantic_link_tests PRIVATE semantic_link)
        add_test(NAME semantic_link_tests COMMAND semantic_link_tests)
    endif()
endif()
