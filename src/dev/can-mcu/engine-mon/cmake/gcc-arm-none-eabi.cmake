set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size CACHE FILEPATH "size")

set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage -fno-exceptions -fno-rtti -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
# Cache entries, not the *_INIT variables: CMake appends its defaults
# (-O3 -DNDEBUG for Release) to *_INIT, and the last -O wins. Existing build
# directories keep their old cache: configure them with --fresh once.
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3" CACHE STRING "C compiler flags for Debug builds")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3" CACHE STRING "C++ compiler flags for Debug builds")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0 -DNDEBUG" CACHE STRING "C compiler flags for Release builds")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0 -DNDEBUG" CACHE STRING "C++ compiler flags for Release builds")

get_filename_component(ENGINE_MON_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Started by the can-mcu bootloader: application at 0x08004000 with an image
# header (src/lib/b1s/FIRMWARE.md), not the CubeMX script at 0x08000000.
set(LINKER_SCRIPT "${ENGINE_MON_DIR}/../common/can-mcu-app.ld")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -T\"${LINKER_SCRIPT}\" --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
