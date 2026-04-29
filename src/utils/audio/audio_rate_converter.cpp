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
    /**
     * @brief Headroom appended to the soxr staging buffer.
     *
     * Per-call output is bounded by ceil(maxInputFrames * tgt / src), but
     * libsoxr's filter delay line can briefly emit a handful more samples
     * during steady-state catch-up after warmup. The 64-sample pad is well
     * above any plausible jitter and stays small enough to be inconsequential
     * versus the per-converter heap footprint.
     */
    constexpr std::size_t STAGING_HEADROOM_SAMPLES{64};

    [[nodiscard]] long long ceilDiv(long long num, long long den) {
        return (num + den - 1) / den;
    }

    [[nodiscard]] int alignedMaxInputFrames(int targetOutputFrames, int sourceRateHz, int targetRateHz) {
        const long long ceiled{ceilDiv(static_cast<long long>(targetOutputFrames) * sourceRateHz, targetRateHz)};
        if (ceiled < 1LL || ceiled > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument{"AudioRateConverter input frame count overflows int"};
        }
        return static_cast<int>(ceiled);
    }
}  // namespace

AudioRateConverter::AudioRateConverter(int sourceRateHz, int targetRateHz, int targetOutputFrames)
    : outputFrames_{0}, sourceRate_{0}, targetRate_{0}, maxInputFrames_{0}, inputAccumulator_{0}, spillReadPos_{0} {
    if (sourceRateHz < 1 || targetRateHz < 1 || targetOutputFrames < 1) {
        throw std::invalid_argument{"Invalid AudioRateConverter parameters"};
    }

    outputFrames_ = targetOutputFrames;
    sourceRate_   = sourceRateHz;
    targetRate_   = targetRateHz;

    if (sourceRateHz == targetRateHz) {
        // Passthrough: identical rates, every call accepts and emits the
        // same fixed block. resampler_ stays nullopt; process() will memcpy.
        maxInputFrames_ = targetOutputFrames;
        return;
    }

    maxInputFrames_ = alignedMaxInputFrames(targetOutputFrames, sourceRateHz, targetRateHz);

    // Staging buffer: max output soxr could produce from maxInputFrames_ inputs.
    // ceil(maxInputFrames * tgt / src) is the rate-ratio bound; STAGING_HEADROOM
    // covers libsoxr's per-call jitter so the call is guaranteed to consume
    // every input frame (idone == ilen) instead of stalling at the output cap.
    const long long maxStagingLL{
        ceilDiv(static_cast<long long>(maxInputFrames_) * targetRateHz, sourceRateHz)};
    if (maxStagingLL > static_cast<long long>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument{"AudioRateConverter staging size overflows int"};
    }
    stagingBuffer_.assign(static_cast<std::size_t>(maxStagingLL) + STAGING_HEADROOM_SAMPLES, 0.0F);

    // The spill rarely holds more than a few samples in steady state, but
    // a pre-reserve avoids reallocations on the rare jitter events.
    spill_.reserve(STAGING_HEADROOM_SAMPLES);

    resampler_.emplace(sourceRateHz, targetRateHz);
}

int AudioRateConverter::outputFrames() const {
    return outputFrames_;
}

int AudioRateConverter::maxInputFrames() const {
    return maxInputFrames_;
}

int AudioRateConverter::peekNextInputFrames() const {
    if (resampler_ == std::nullopt) {
        return outputFrames_;
    }

    // Bresenham accumulator state invariant:
    //   inputAccumulator_ == K * outputFrames * sourceRate - sum(n_i) * targetRate
    // and inputAccumulator_ stays in [0, targetRate) after each process() call.
    // The next n is the largest integer such that n * targetRate <= total, which
    // makes the cumulative input track K * outputFrames * sourceRate / targetRate
    // exactly (rounded to the nearest integer at each step).
    const long long total{inputAccumulator_ + static_cast<long long>(outputFrames_) * sourceRate_};
    return static_cast<int>(total / targetRate_);
}

void AudioRateConverter::reset() {
    inputAccumulator_ = 0;
    spill_.clear();
    spillReadPos_ = 0;
    if (resampler_ != std::nullopt) {
        resampler_.value().clear();
    }
}

bool AudioRateConverter::process(std::span<const float> in, std::span<float> out) {
    const int expectedIn{peekNextInputFrames()};
    // expectedIn must be strictly positive: a zero-sized input span would
    // dispatch to libsoxr's documented end-of-stream flush form, desynchronising
    // the resampler for any subsequent calls. The audio_pipeline rate range
    // (>= 8000 Hz input, output frames >= 1024) makes this unreachable, but
    // the explicit guard documents the invariant.
    if (expectedIn <= 0 || in.size() != static_cast<std::size_t>(expectedIn) ||
        out.size() != static_cast<std::size_t>(outputFrames_)) {
        return false;
    }

    if (resampler_ == std::nullopt) {
        // Passthrough: no Bresenham state, no soxr state - input and output
        // spans have identical size by ctor invariant; std::copy is the
        // canonical zero-overhead memcpy here.
        std::copy(in.begin(), in.end(), out.begin());
        return true;
    }

    // Advance Bresenham accumulator now that we know how much input we are
    // about to consume. Net effect: accumulator stays in [0, targetRate_),
    // and over K calls the cumulative input matches K * outputFrames *
    // sourceRate / targetRate exactly.
    inputAccumulator_ += static_cast<long long>(outputFrames_) * sourceRate_;
    inputAccumulator_ -= static_cast<long long>(expectedIn) * targetRate_;

    // 1. Drain any spill from previous call into the head of out.
    std::size_t outIdx{0};
    {
        const std::size_t available{spill_.size() - spillReadPos_};
        const std::size_t take{std::min(available, out.size())};
        std::copy_n(spill_.begin() + static_cast<std::ptrdiff_t>(spillReadPos_), take, out.begin());
        spillReadPos_ += take;
        outIdx += take;
        if (spillReadPos_ >= spill_.size()) {
            spill_.clear();
            spillReadPos_ = 0;
        }
    }

    // 2. Run soxr into the staging buffer. The staging buffer is sized so
    // soxr always consumes the full input (idone == ilen); we ignore
    // inputConsumed because it is structurally guaranteed to equal in.size().
    const auto result{resampler_.value().process(
        in, std::span<float>{stagingBuffer_.data(), stagingBuffer_.size()})};
    if (result == std::nullopt) {
        return false;
    }
    const std::size_t produced{result.value().outputProduced};

    // 3. Copy as many staging samples as fit into the remaining out slots;
    // overflow goes into the spill for next call.
    const std::size_t toOut{std::min(produced, out.size() - outIdx)};
    std::copy_n(stagingBuffer_.begin(), toOut, out.begin() + static_cast<std::ptrdiff_t>(outIdx));
    outIdx += toOut;

    if (produced > toOut) {
        // Compact any leftover spill head before appending so spill_.size()
        // stays bounded by per-call jitter rather than growing across calls.
        if (spillReadPos_ > 0) {
            spill_.erase(spill_.begin(), spill_.begin() + static_cast<std::ptrdiff_t>(spillReadPos_));
            spillReadPos_ = 0;
        }
        spill_.insert(spill_.end(),
                      stagingBuffer_.begin() + static_cast<std::ptrdiff_t>(toOut),
                      stagingBuffer_.begin() + static_cast<std::ptrdiff_t>(produced));
    }

    // 4. Filter warmup: on the very first calls soxr has not yet seen
    // enough input to compute every requested output position - the tail of
    // out is padded with silence (typically a few hundred samples on the
    // first call, zero from the second onward). Steady state with Bresenham
    // input sizing always fills out in full.
    if (outIdx < out.size()) {
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(outIdx), out.end(), 0.0F);
    }
    return true;
}
