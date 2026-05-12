# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 05.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Unit tests for the shared audio utility library
# ----------------------------------------------------------
# libsndfile and libsoxr are PRIVATE deps of rpitx_audio_utils, so the test
# target re-imports libsndfile (PkgConfig::SNDFILE was declared by the parent
# directory and is visible through inheritance) for tests that drive the
# libsndfile factory directly with WAV files written from the test harness.
add_executable(rpitx_audio_utils_tests
    test_audio_pipeline.cpp
    test_audio_rate_converter.cpp
    test_libsndfile_audio_source.cpp
    test_soxr_resampler.cpp
    test_validate_helpers.cpp
)

target_link_libraries(rpitx_audio_utils_tests
    PRIVATE
        rpitx_audio_utils
        PkgConfig::SNDFILE
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
)

gtest_discover_tests(rpitx_audio_utils_tests
    PROPERTIES LABELS "unit;audio"
)
