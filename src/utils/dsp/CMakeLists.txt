# ----------------------------------------------------------
# Shared DSP utility library
# ----------------------------------------------------------
add_library(rpitx_dsp_utils STATIC
    agc.cpp
    biquad.cpp
    hilbert.cpp
)
target_include_directories(rpitx_dsp_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_dsp_utils PRIVATE m)
