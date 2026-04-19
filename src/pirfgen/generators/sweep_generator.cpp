/**
 * @file sweep_generator.cpp
 * @brief SweepGenerator implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "sweep_generator.h"

#include <algorithm>

SweepGenerator::SweepGenerator(float bandwidth, uint32_t sampleRate)
    : sawtooth_{bandwidth * 0.5F, std::max(1, static_cast<int>(static_cast<float>(sampleRate) / RATE_HZ))} {
}

float SweepGenerator::nextSample() {
    return sawtooth_.nextSample();
}
