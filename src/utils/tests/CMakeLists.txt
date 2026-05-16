# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 15.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared test utility headers
# ----------------------------------------------------------
# Header-only library that exposes infrastructure shared across the unit-test
# binaries: a gtest fixture-mixin that captures stdout / stderr for the
# duration of a test (used by every test that asserts on user-visible
# diagnostics). Test executables link this as an INTERFACE dep so the include
# directory propagates without an artifact to link.
# ----------------------------------------------------------
add_library(rpitx_common_test_utils INTERFACE)
target_include_directories(rpitx_common_test_utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
