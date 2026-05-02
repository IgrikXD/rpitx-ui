# Author: Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
# Date: 02.05.2026
# License: GPL-3.0
# Fork: https://github.com/IgrikXD/rpitx-ui
# RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.

# ----------------------------------------------------------
# Shared DSP utility library
# ----------------------------------------------------------
# Provides the lightweight signal-processing primitives shared by every
# transmitter binary: an envelope-tracking automatic gain control (AGC) that
# usable with both IqSample pairs and scalar audio, a Direct-Form-I
# second-order IIR Biquad with Butterworth high/low-pass and FM
# pre-emphasis factories, a Blackman-windowed Hilbert FIR transformer that
# turns a real input into a delay-aligned analytic (I/Q) signal, and the
# common IqSample value type that ties these stages together into a
# pipeline.
# ----------------------------------------------------------
add_library(rpitx_dsp_utils STATIC
    biquad.cpp
    hilbert.cpp
)
target_include_directories(rpitx_dsp_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_dsp_utils PRIVATE m)
