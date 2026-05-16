# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 15.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared audio utility library
# ----------------------------------------------------------
# Provides the audio front-end shared by every transmitter binary:
# a libsndfile-backed file source (AudioSource / LibsndfileAudioSource),
# a libsoxr-backed streaming rate converter (SoxrResampler /
# AudioRateConverter), and an AudioPipeline that wires source -> loop-aware
# block reader -> channel adapter -> per-channel rate conversion to deliver
# fixed-size interleaved float blocks at the caller's target sample rate.
# ----------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)

add_library(rpitx_audio_utils STATIC
    audio_pipeline.cpp
    audio_rate_converter.cpp
    libsndfile_audio_source.cpp
    soxr_resampler.cpp
)
target_include_directories(rpitx_audio_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_audio_utils PRIVATE PkgConfig::SNDFILE PkgConfig::SOXR)

if(ENABLE_TESTING)
    add_subdirectory(tests)
endif()
