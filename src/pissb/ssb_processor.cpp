/**
 * @file ssb_processor.cpp
 * @brief SSB processor implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "ssb_processor.h"

SsbProcessor::SsbProcessor(SsbMode mode) : mode_{mode} {
}

IqSample SsbProcessor::process(float sample) {
    // Bandpass 300-3000 Hz
    const float filtered{lpf_.process(hpf_.process(sample))};

    // Hilbert transform -> analytic signal
    auto [i, q]{hilbert_.process(filtered)};

    // LSB: negate Q to mirror spectrum
    if (mode_ == SsbMode::LSB) {
        q = -q;
    }

    // AGC
    return agc_.process({.i = i, .q = q});
}
