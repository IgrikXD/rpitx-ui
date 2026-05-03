# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 29.04.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared CLI parsing utility library
# ----------------------------------------------------------
# Provides the command-line front-end shared by every migrated transmitter
# binary: a thin CLI11 wrapper that routes help to stdout and parse errors
# to stderr, a frequency parser that accepts decimal and scientific notation
# resolving to integer Hz, and reusable CLI11 validators bound to a uniform
# ParseResult outcome enum.
# ----------------------------------------------------------
find_package(CLI11 CONFIG REQUIRED)
add_library(rpitx_cli_utils STATIC
    cli_common.cpp
    cli_validators.cpp
)
target_include_directories(rpitx_cli_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_cli_utils PUBLIC CLI11::CLI11)
