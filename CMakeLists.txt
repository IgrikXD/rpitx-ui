# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 27.03.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

cmake_minimum_required(VERSION 3.25)
project(rpitx-ui
    VERSION 1.9
    DESCRIPTION "RF transmitter for Raspberry Pi with improved UI functionality, built with CMake."
    LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------
# Compiler version check
# ----------------------------------------------------------
# Floating-point std::from_chars (used in src/utils/cli_utils.h) requires
# libstdc++ >= 11, which ships with GCC 11+.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
        message(FATAL_ERROR
            "rpitx-ui requires GCC >= 11 (detected ${CMAKE_CXX_COMPILER_VERSION}). "
            "Floating-point std::from_chars is unsupported on older libstdc++. "
            "Upgrade the toolchain to GCC 11 or newer to build rpitx-ui!"
        )
    endif()
else()
    message(WARNING
        "rpitx-ui requires GCC >= 11. Detected compiler: "
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
        "Build may fail on floating-point std::from_chars."
    )
endif()

# ----------------------------------------------------------
# Raspberry Pi platform check
# ----------------------------------------------------------
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    message(FATAL_ERROR
        "rpitx-ui requires a Raspberry Pi (ARM). "
        "Detected processor: ${CMAKE_SYSTEM_PROCESSOR}"
    )
endif()

if(EXISTS "/proc/device-tree/model")
    file(READ "/proc/device-tree/model" RPI_MODEL)
    if(NOT RPI_MODEL MATCHES "Raspberry Pi")
        message(FATAL_ERROR
            "rpitx-ui requires a Raspberry Pi. "
            "Detected board: ${RPI_MODEL}"
        )
    endif()
    message(STATUS "Board: ${RPI_MODEL}")
else()
    message(WARNING
        "/proc/device-tree/model not found - cannot verify Raspberry Pi board. "
        "Build may fail at runtime on non-RPi ARM hardware."
    )
endif()

# ----------------------------------------------------------
# Build parameters
# ----------------------------------------------------------
# Common compile options (optimization and -g handled by CMAKE_BUILD_TYPE)
add_compile_options(
    -Wall
    -Wno-unused-variable
)

# Option responsible for building optional targets (not used by easytest.sh directly)
option(BUILD_OPTIONAL_TARGETS "Build optional targets not used by easytest.sh" ON)

add_subdirectory(src)
