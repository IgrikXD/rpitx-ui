/**
 * @file am_processor.cpp
 * @brief AM envelope processor implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 20.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "am_processor.h"

#include <algorithm>

AmProcessor::AmProcessor(float sampleRate)
    : hpf_{Biquad::highPass(HPF_CUTOFF, sampleRate)}, lpf_{Biquad::lowPass(LPF_CUTOFF, sampleRate)} {
}

float AmProcessor::process(float sample) {
    const float filtered{lpf_.process(hpf_.process(sample))};
    const float agcd{agc_.process(filtered)};

    // DSB-FC AM envelope: s = 0.5 * (1 + m * a). Clamp guards against
    // transient AGC gain overshoot (env_ briefly lagging a sudden level
    // jump) pushing the peak past unity, which would drive the amdmasync
    // pad quantizer into saturation.
    const float envelope{0.5f * (1.0f + MODULATION_DEPTH * agcd)};
    return std::clamp(envelope, 0.0f, 1.0f);
}
