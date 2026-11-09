# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 06.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared real-time utility library
# ----------------------------------------------------------
# Provides what keeps the thread that feeds the DMA buffer in step with real
# time: the flush-to-zero floating-point mode, which keeps decaying filter
# states out of slow subnormal arithmetic, and real-time scheduling, which
# runs the thread ahead of every normally scheduled process.
# ----------------------------------------------------------
add_library(rpitx_realtime_utils STATIC
    realtime_scheduling.cpp
)
target_include_directories(rpitx_realtime_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
