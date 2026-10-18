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
set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics")
# Both build types are optimized for size: the bootloader must fit 16 KB.
set(CMAKE_C_FLAGS_DEBUG "-Os -g3" CACHE STRING "C compiler flags for Debug builds")
set(CMAKE_CXX_FLAGS_DEBUG "-Os -g3" CACHE STRING "C++ compiler flags for Debug builds")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0 -DNDEBUG" CACHE STRING "C compiler flags for Release builds")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0 -DNDEBUG" CACHE STRING "C++ compiler flags for Release builds")

get_filename_component(BOOTLOADER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -T\"${BOOTLOADER_DIR}/bootloader.ld\" -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
