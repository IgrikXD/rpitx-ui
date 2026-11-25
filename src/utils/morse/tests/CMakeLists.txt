# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 25.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared Morse code utility library
# ----------------------------------------------------------
add_executable(rpitx_morse_utils_tests
    test_morse_encoder.cpp
)

target_link_libraries(rpitx_morse_utils_tests
    PRIVATE
        rpitx_morse_utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rpitx_morse_utils_tests
    PROPERTIES LABELS "unit;morse"
)
