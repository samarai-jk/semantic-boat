if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(semantic_display LANGUAGES CXX)
endif()

add_library(semantic_display STATIC
    src/compiler.cpp
    src/data.cpp
    src/display.cpp
    src/history.cpp
    src/package.cpp
    src/storage.cpp
)

target_include_directories(semantic_display PUBLIC include)
target_compile_features(semantic_display PUBLIC cxx_std_17)
if(TARGET slstm32)
    target_link_libraries(semantic_display PUBLIC slstm32)
elseif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(../slstm32 ${CMAKE_CURRENT_BINARY_DIR}/slstm32)
    target_link_libraries(semantic_display PUBLIC slstm32)
endif()
target_compile_options(semantic_display PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(semantic_display_tests tests/compiler_tests.cpp)
        target_link_libraries(semantic_display_tests PRIVATE semantic_display)
        add_test(NAME semantic_display_tests COMMAND semantic_display_tests)

        add_executable(semantic_display_runtime_tests tests/runtime_tests.cpp)
        target_link_libraries(semantic_display_runtime_tests PRIVATE semantic_display)
        add_test(NAME semantic_display_runtime_tests COMMAND semantic_display_runtime_tests)

        add_executable(semantic_display_storage_tests tests/storage_tests.cpp)
        target_link_libraries(semantic_display_storage_tests PRIVATE semantic_display)
        add_test(NAME semantic_display_storage_tests COMMAND semantic_display_storage_tests)
    endif()
endif()
