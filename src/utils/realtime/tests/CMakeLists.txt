# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 06.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared real-time utility library
# ----------------------------------------------------------
add_executable(rpitx_realtime_utils_tests
    test_flush_to_zero.cpp
    test_realtime_scheduling.cpp
)

target_link_libraries(rpitx_realtime_utils_tests
    PRIVATE
        rpitx_realtime_utils
        rpitx_dsp_utils
        rpitx_common_test_utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rpitx_realtime_utils_tests
    PROPERTIES LABELS "unit;realtime"
)
