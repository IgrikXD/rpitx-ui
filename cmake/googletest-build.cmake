# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 05.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# GoogleTest / GoogleMock fetched as part of the test build
# ----------------------------------------------------------
# Pulled via FetchContent so the test suite is self-contained: CI / contributor
# machines do not need a system-wide libgtest-dev.
# ----------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)

# Suppress GoogleTest's own install() rules so `cmake --install` for rpitx-ui
# does not leak gtest headers and archives into /usr.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# Build GoogleMock alongside GoogleTest.
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)
