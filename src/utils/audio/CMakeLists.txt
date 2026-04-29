# ----------------------------------------------------------
# Shared audio utility library
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
