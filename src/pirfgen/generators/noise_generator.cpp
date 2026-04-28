/**
 * @file noise_generator.cpp
 * @brief NoiseGenerator implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "noise_generator.h"

#include <algorithm>

NoiseGenerator::NoiseGenerator(float bandwidth, int sampleRate)
    : dist_{-bandwidth * 0.5F, bandwidth * 0.5F},
      holdSamples_{computeHoldSamples(bandwidth, sampleRate)},
      current_{dist_(engine_)} {
}

float NoiseGenerator::nextSample() {
    if (counter_ >= holdSamples_) {
        counter_ = 0;
        current_ = dist_(engine_);
    }
    ++counter_;
    return current_;
}

int NoiseGenerator::computeHoldSamples(float bandwidth, int sampleRate) {
    const float modBw{bandwidth * MOD_BW_FRACTION};
    return std::max(1, static_cast<int>(static_cast<float>(sampleRate) / modBw));
}
