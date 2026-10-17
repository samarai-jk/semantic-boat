# Firmware build profiles: keeps development and test code out of product
# firmware. Rules: AGENTS.md, section "Firmware code categories and build
# profiles". Every firmware target must call sb_firmware_profile() exactly once.
#
#   sb_firmware_profile(<target> LAB|PRODUCTION
#       [DEV_SOURCES <files...>]     compiled only with development tools
#       [STUB_SOURCES <files...>])   compiled only without them (no-op stand-ins)
#
# LAB         a lab or bring-up device; development tools default to ON.
# PRODUCTION  a product; development tools default to OFF, and a Release
#             build with development tools stops with an error.
#
# The cache variable SB_DEV_TOOLS (AUTO, ON, OFF; default AUTO) overrides the
# default, for example to check that a lab device also builds as a product
# (-DSB_DEV_TOOLS=OFF) or to debug a product with its development tools
# (-DSB_DEV_TOOLS=ON in a Debug build).
#
# The target receives the compile definitions SB_DEV_TOOLS (0/1) and
# SB_PRODUCTION (0/1); code reads them through slstm32/build_profile.hpp.

include_guard(GLOBAL)

set(SB_DEV_TOOLS "AUTO" CACHE STRING "Development and test tools in firmware: AUTO (by profile), ON, OFF")
set_property(CACHE SB_DEV_TOOLS PROPERTY STRINGS AUTO ON OFF)

function(sb_firmware_profile target)
    cmake_parse_arguments(PROFILE "LAB;PRODUCTION" "" "DEV_SOURCES;STUB_SOURCES" ${ARGN})
    if(PROFILE_LAB AND PROFILE_PRODUCTION OR NOT PROFILE_LAB AND NOT PROFILE_PRODUCTION)
        message(FATAL_ERROR "sb_firmware_profile(${target}): choose exactly one of LAB or PRODUCTION")
    endif()
    get_target_property(already ${target} SB_FIRMWARE_PROFILE)
    if(already)
        message(FATAL_ERROR "sb_firmware_profile(${target}): called more than once")
    endif()

    if(SB_DEV_TOOLS STREQUAL "AUTO")
        set(dev_tools ${PROFILE_LAB})
    elseif(SB_DEV_TOOLS)
        set(dev_tools TRUE)
    else()
        set(dev_tools FALSE)
    endif()

    if(PROFILE_PRODUCTION)
        set(profile PRODUCTION)
        if(dev_tools AND CMAKE_BUILD_TYPE STREQUAL "Release")
            message(FATAL_ERROR
                "${target} is a PRODUCTION firmware: a Release build must not contain development tools. "
                "Build Debug, or remove -DSB_DEV_TOOLS=ON. See AGENTS.md.")
        endif()
    else()
        set(profile LAB)
    endif()

    if(dev_tools)
        target_sources(${target} PRIVATE ${PROFILE_DEV_SOURCES})
        target_compile_definitions(${target} PRIVATE SB_DEV_TOOLS=1)
        set(tools_text "with development tools")
    else()
        target_sources(${target} PRIVATE ${PROFILE_STUB_SOURCES})
        target_compile_definitions(${target} PRIVATE SB_DEV_TOOLS=0)
        set(tools_text "without development tools")
    endif()
    if(PROFILE_PRODUCTION)
        target_compile_definitions(${target} PRIVATE SB_PRODUCTION=1)
    else()
        target_compile_definitions(${target} PRIVATE SB_PRODUCTION=0)
    endif()
    set_target_properties(${target} PROPERTIES SB_FIRMWARE_PROFILE ${profile})
    message(STATUS "Firmware ${target}: ${profile} profile, ${tools_text}")
endfunction()
