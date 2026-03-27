/**
 * @file ssb_processor.cpp
 * @brief SSB processor implementation.
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
