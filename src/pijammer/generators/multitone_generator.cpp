/**
 * @file multitone_generator.cpp
 * @brief MultitoneGenerator implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "multitone_generator.h"

#include <algorithm>

MultitoneGenerator::MultitoneGenerator(float bandwidth, uint32_t sampleRate, int toneCount)
    : dist_{0, toneCount - 1},
      tones_{makeTones(bandwidth, toneCount)},
      samplesPerHop_{computeSamplesPerHop(sampleRate)},
      idx_{dist_(engine_)} {
}

float MultitoneGenerator::nextSample() {
    if (counter_ >= samplesPerHop_) {
        counter_ = 0;
        idx_     = dist_(engine_);
    }
    ++counter_;
    return tones_[static_cast<std::size_t>(idx_)];
}

std::vector<float> MultitoneGenerator::makeTones(float bandwidth, int toneCount) {
    const float halfBw{bandwidth * 0.5F};
    const float slot{bandwidth / static_cast<float>(toneCount)};
    const float base{-halfBw + slot * 0.5F};

    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(toneCount));
    for (int i{0}; i < toneCount; ++i) {
        result.push_back(base + slot * static_cast<float>(i));
    }
    return result;
}

int MultitoneGenerator::computeSamplesPerHop(uint32_t sampleRate) {
    return std::max(1, static_cast<int>(static_cast<float>(sampleRate) / HOP_RATE_HZ));
}
