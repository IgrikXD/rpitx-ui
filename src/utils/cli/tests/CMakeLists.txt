# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 15.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared CLI parsing utility library
# ----------------------------------------------------------
add_executable(rpitx_cli_utils_tests
    test_parse_frequency.cpp
    test_parse_cli_app.cpp
    test_validators.cpp
)

target_link_libraries(rpitx_cli_utils_tests
    PRIVATE
        rpitx_cli_utils
        rpitx_common_test_utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rpitx_cli_utils_tests
    PROPERTIES LABELS "unit;cli"
)
