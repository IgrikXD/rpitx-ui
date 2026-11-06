/**
 * @file signal_continuity.h
 * @brief Test helpers that expose silence inserted into a converted signal.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <ranges>
#include <span>
#include <vector>

/**
 * @brief Build a 1 kHz tone on a DC offset that keeps every sample between 0.25 and 0.75.
 *
 * The signal never comes near zero, so a zero sample in anything derived from it by rate
 * conversion can only be padding.
 *
 * @param sampleRateHz Sample rate of the tone in Hz.
 * @param frames       Number of samples to generate.
 */
[[nodiscard]] inline std::vector<float> makeOffsetTone(int sampleRateHz, std::size_t frames) {
    constexpr float kOffset{0.5F};
    constexpr float kAmplitude{0.25F};
    constexpr float kToneHz{1'000.0F};
    const float omega{2.0F * std::numbers::pi_v<float> * kToneHz / static_cast<float>(sampleRateHz)};

    std::vector<float> tone(frames);
    for (std::size_t n{0}; n < frames; ++n) {
        tone[n] = kOffset + kAmplitude * std::sin(omega * static_cast<float>(n));
    }
    return tone;
}

/**
 * @brief Count the zero samples that lie between the first and the last non-zero sample.
 *
 * Leading and trailing zeros are not counted: only silence that is followed by signal again
 * is a gap inside the audio.
 */
[[nodiscard]] inline std::size_t countSilenceInsideSignal(std::span<const float> samples) {
    const auto isSignal{[](float sample) { return sample != 0.0F; }};
    const auto firstSignal{std::ranges::find_if(samples, isSignal)};
    const auto afterLastSignal{std::ranges::find_if(samples | std::views::reverse, isSignal).base()};
    if (firstSignal == samples.end()) {
        return 0;
    }
    return static_cast<std::size_t>(std::ranges::count(firstSignal, afterLastSignal, 0.0F));
}
