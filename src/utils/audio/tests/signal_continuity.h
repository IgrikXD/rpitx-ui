/**
 * @file signal_continuity.h
 * @brief Test helpers that expose silence, drops and repeats in a converted signal.
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
 * @brief Sample of a 1 kHz tone on a DC offset that keeps every sample between 0.25 and 0.75.
 *
 * The signal never comes near zero, so a zero sample in anything derived from it by rate
 * conversion can only be padding. The phase is computed in double, so the samples of a signal
 * at the target rate serve as the exact reference for the converted samples.
 *
 * @param sampleRateHz Sample rate of the tone in Hz.
 * @param index        Index of the sample.
 */
[[nodiscard]] inline float offsetToneSample(int sampleRateHz, std::size_t index) {
    constexpr double kOffset{0.5};
    constexpr double kAmplitude{0.25};
    constexpr double kToneHz{1'000.0};
    const double phase{2.0 * std::numbers::pi * kToneHz * static_cast<double>(index) / sampleRateHz};
    return static_cast<float>(kOffset + kAmplitude * std::sin(phase));
}

/**
 * @brief Build frames samples of the offset tone at sampleRateHz.
 */
[[nodiscard]] inline std::vector<float> makeOffsetTone(int sampleRateHz, std::size_t frames) {
    std::vector<float> tone(frames);
    for (std::size_t n{0}; n < frames; ++n) {
        tone[n] = offsetToneSample(sampleRateHz, n);
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

/**
 * @brief Count the samples that differ from the offset tone at sampleRateHz by more than tolerance.
 *
 * Sample i of a converted signal is compared with sample i of the tone at the target rate,
 * so a dropped, repeated or reordered sample shows as a phase error from that point on. The
 * first and the last skipFrames samples are left out, where the filter transients of the
 * start and of the end of the input dominate.
 */
[[nodiscard]] inline std::size_t countSamplesOffTone(std::span<const float> samples, int sampleRateHz,
                                                     std::size_t skipFrames, float tolerance) {
    if (samples.size() <= 2 * skipFrames) {
        return 0;
    }
    std::size_t offTone{0};
    for (std::size_t i{skipFrames}; i < samples.size() - skipFrames; ++i) {
        if (std::abs(samples[i] - offsetToneSample(sampleRateHz, i)) > tolerance) {
            ++offTone;
        }
    }
    return offTone;
}
