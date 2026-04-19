/**
 * @file sawtooth.cpp
 * @brief Sawtooth oscillator implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "sawtooth.h"

#include <algorithm>

Sawtooth::Sawtooth(float amplitude, int samplesPerCycle)
    : amplitude_{amplitude},
      step_{2.0f * amplitude / static_cast<float>(std::max(1, samplesPerCycle))},
      pos_{-amplitude} {
}

float Sawtooth::nextSample() {
    const float out{pos_};
    pos_ += step_;
    if (pos_ >= amplitude_) {
        pos_ -= 2.0f * amplitude_;
    }
    return out;
}
