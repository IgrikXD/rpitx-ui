# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 15.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Project-specific shared utility libraries
# ----------------------------------------------------------
# Shared test utilities go first so the rpitx_common_test_utils INTERFACE
# target is already declared when per-utility test subdirectories link
# against it.
if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(dsp)
add_subdirectory(audio)
add_subdirectory(cli)
