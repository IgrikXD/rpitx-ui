# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 09.11.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared image utility library
# ----------------------------------------------------------
add_executable(rpitx_image_utils_tests
    test_pnm_reader.cpp
)

target_link_libraries(rpitx_image_utils_tests
    PRIVATE
        rpitx_image_utils
        rpitx_common_test_utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rpitx_image_utils_tests
    PROPERTIES LABELS "unit;image"
)
