# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 10.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared DMA utility library
# ----------------------------------------------------------
# Provides the stop of a librpitx DMA transmission: when the input has ended,
# the samples still queued in the ring are played before the engine stops
# instead of being cut together with the ring.
# ----------------------------------------------------------
add_library(rpitx_dma_utils STATIC
    dma_stop.cpp
)
target_include_directories(rpitx_dma_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_dma_utils PUBLIC librpitx::librpitx)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
