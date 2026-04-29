/**
 * @file audio_rate_converter.cpp
 * @brief AudioRateConverter implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 29.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "audio_rate_converter.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
    [[nodiscard]] int alignedInputForOutput(int targetOutputFrames, int sourceRateHz, int targetRateHz) {
        // ceil(targetOutputFrames * sourceRateHz / targetRateHz). Over-feeding by
        // up to one input sample per call keeps soxr's internal state ahead of
        // the output position, so steady-state calls always fill the output.
        const long long num{static_cast<long long>(targetOutputFrames) * static_cast<long long>(sourceRateHz)};
        const long long ceiled{(num + static_cast<long long>(targetRateHz) - 1LL) / static_cast<long long>(targetRateHz)};
        if (ceiled < 1LL || ceiled > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument{"AudioRateConverter input frame count overflows int"};
        }
        return static_cast<int>(ceiled);
    }
}  // namespace

AudioRateConverter::AudioRateConverter(int sourceRateHz, int targetRateHz, int targetOutputFrames)
    : inputFrames_{0}, outputFrames_{0} {
    if (sourceRateHz < 1 || targetRateHz < 1 || targetOutputFrames < 1) {
        throw std::invalid_argument{"Invalid AudioRateConverter parameters"};
    }

    outputFrames_ = targetOutputFrames;

    if (sourceRateHz == targetRateHz) {
        // Passthrough: identical rates, block size is verbatim the requested
        // output target. resampler_ stays nullopt; process() will memcpy.
        inputFrames_ = targetOutputFrames;
        return;
    }

    inputFrames_ = alignedInputForOutput(targetOutputFrames, sourceRateHz, targetRateHz);
    resampler_.emplace(sourceRateHz, targetRateHz);
}

int AudioRateConverter::inputFrames() const {
    return inputFrames_;
}

int AudioRateConverter::outputFrames() const {
    return outputFrames_;
}

bool AudioRateConverter::process(std::span<const float> in, std::span<float> out) {
    if (in.size() != static_cast<std::size_t>(inputFrames_) || out.size() != static_cast<std::size_t>(outputFrames_)) {
        return false;
    }

    if (resampler_ == std::nullopt) {
        // Passthrough: input and output spans have identical size by ctor
        // invariant; std::copy is the canonical zero-overhead memcpy here.
        std::copy(in.begin(), in.end(), out.begin());
        return true;
    }

    const auto firstResult{resampler_.value().process(in, out)};
    if (firstResult == std::nullopt) {
        return false;
    }
    std::size_t produced{firstResult.value().outputProduced};

    if (produced < out.size()) {
        // Filter warmup: soxr has not yet seen enough input to compute outputs
        // for the tail of this block. A flush call with empty input drains
        // any samples already buffered in the filter delay line; subsequent
        // calls reach steady state where soxr fills out completely.
        const auto flushResult{resampler_.value().process({}, out.subspan(produced))};
        if (flushResult != std::nullopt) {
            produced += flushResult.value().outputProduced;
        }
    }

    if (produced < out.size()) {
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(produced), out.end(), 0.0F);
    }
    return true;
}
