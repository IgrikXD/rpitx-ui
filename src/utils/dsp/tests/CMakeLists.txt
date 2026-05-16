# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 05.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared DSP utility library
# ----------------------------------------------------------
add_executable(rpitx_dsp_utils_tests
    test_agc.cpp
    test_biquad.cpp
    test_hilbert.cpp
    test_iq_sample.cpp
)

target_link_libraries(rpitx_dsp_utils_tests
    PRIVATE
        rpitx_dsp_utils
        GTest::gtest
        GTest::gtest_main
)

# CTest integration: discover each TEST/TEST_P/TEST_F as an individual ctest
# entry so a failing case is reported by name instead of being lumped under
# the whole binary.
gtest_discover_tests(rpitx_dsp_utils_tests
    PROPERTIES LABELS "unit;dsp"
)
