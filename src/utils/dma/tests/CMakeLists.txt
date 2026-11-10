# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 10.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared DMA utility library
# ----------------------------------------------------------
add_executable(rpitx_dma_utils_tests
    test_dma_transmission.cpp
)

target_link_libraries(rpitx_dma_utils_tests
    PRIVATE
        rpitx_dma_utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rpitx_dma_utils_tests
    PROPERTIES LABELS "unit;dma"
)
