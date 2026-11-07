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
     * libsoxr accepts no more input per call than its output buffer can hold
     * at the rate ratio. The pad keeps that limit above the largest input
     * block, so every call consumes its whole input; output that does not
     * fit stays inside libsoxr until the next call.
     */
    constexpr std::size_t STAGING_HEADROOM_SAMPLES{64};

    [[nodiscard]] constexpr long long ceilDiv(long long num, long long den) noexcept {
        return (num + den - 1) / den;
    }

    [[nodiscard]] std::size_t alignedMaxInputFrames(std::size_t targetOutputFrames, int sourceRateHz,
                                                    int targetRateHz) {
        const long long ceiled{ceilDiv(static_cast<long long>(targetOutputFrames) * sourceRateHz, targetRateHz)};
        // ceiled is bounded below by 1 (positive inputs) and above by the
        // chosen size_t domain - on 32-bit platforms size_t is narrower than
        // long long, so the upper guard is not vacuous.
        if (ceiled < 1LL || static_cast<unsigned long long>(ceiled) > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument{"AudioRateConverter input frame count overflows std::size_t"};
        }
        return static_cast<std::size_t>(ceiled);
    }
}  // namespace

AudioRateConverter::AudioRateConverter(int sourceRateHz, int targetRateHz, std::size_t targetOutputFrames)
    : outputFrames_{0}, sourceRate_{0}, targetRate_{0}, maxInputFrames_{0}, inputAccumulator_{0}, pendingReadPos_{0} {
    if (sourceRateHz < 1 || targetRateHz < 1 || targetOutputFrames == 0) {
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

    // Staging buffer: the output maxInputFrames_ inputs amount to at the rate
    // ratio, ceil(maxInputFrames * tgt / src), plus STAGING_HEADROOM so the
    // call is guaranteed to consume every input frame (idone == ilen)
    // instead of stalling at the output cap.
    const long long maxStagingLL{ceilDiv(static_cast<long long>(maxInputFrames_) * targetRateHz, sourceRateHz)};
    if (maxStagingLL < 1LL || static_cast<unsigned long long>(maxStagingLL) > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument{"AudioRateConverter staging size overflows std::size_t"};
    }
    stagingBuffer_.assign(static_cast<std::size_t>(maxStagingLL) + STAGING_HEADROOM_SAMPLES, 0.0F);

    // The queue holds the start-up reserve plus the latest burst - up to
    // 3.3 staging buffers at 44.1 -> 48 kHz; reserve enough up front so it
    // never reallocates while the transmission runs.
    pendingOutput_.reserve(4 * stagingBuffer_.size());

    resampler_.emplace(sourceRateHz, targetRateHz);
}

std::optional<std::size_t> AudioRateConverter::drain(std::span<float> out) {
    if (out.empty() || resampler_ == std::nullopt) {
        // Passthrough mode has no internal state to drain - the source
        // exhausting was already reported to the caller as End.
        return std::size_t{0};
    }

    // 1. Emit the queued samples first - these are real output the
    // resampler already produced on prior calls.
    std::size_t outIdx{0};
    {
        const std::size_t available{pendingOutput_.size() - pendingReadPos_};
        const std::size_t take{std::min(available, out.size())};
        std::copy_n(pendingOutput_.begin() + pendingReadPos_, take, out.begin());
        pendingReadPos_ += take;
        outIdx += take;
        if (pendingReadPos_ >= pendingOutput_.size()) {
            pendingOutput_.clear();
            pendingReadPos_ = 0;
        }
    }

    if (outIdx >= out.size()) {
        return outIdx;
    }

    // 2. Pull soxr's filter tail with the documented end-of-stream flush
    // form (empty input span). After this call libsoxr is in its post-flush
    // state; reset() must be invoked before resuming normal process() use.
    const auto result{resampler_.value().process(std::span<const float>{}, out.subspan(outIdx))};
    if (result == std::nullopt) {
        // Surface the libsoxr failure as nullopt so the pipeline reports
        // Error, not End - silently treating a flush failure as a clean
        // drain would mask a real fault and lose any queued samples we
        // already emitted into out.
        return std::nullopt;
    }
    outIdx += result.value().outputProduced;
    return outIdx;
}

AudioRateConverterStatus AudioRateConverter::process(std::span<const float> in, std::span<float> out) {
    const std::size_t expectedIn{peekNextInputFrames()};
    // expectedIn must be strictly positive: a zero-sized input span would
    // dispatch to libsoxr's documented end-of-stream flush form, desynchronising
    // the resampler for any subsequent calls. The audio_pipeline rate range
    // (>= 8000 Hz input, output frames >= 1024) makes this unreachable, but
    // the explicit guard documents the invariant.
    if (expectedIn == 0 || in.size() != expectedIn || out.size() != outputFrames_) {
        return AudioRateConverterStatus::Error;
    }

    if (resampler_ == std::nullopt) {
        // Passthrough: no Bresenham state, no soxr state - input and output
        // spans have identical size by ctor invariant; std::copy is the
        // canonical zero-overhead memcpy here.
        std::copy(in.begin(), in.end(), out.begin());
        return AudioRateConverterStatus::Ok;
    }

    // Compute the next Bresenham accumulator locally; commit only after the
    // soxr call below succeeds so a mid-process failure leaves the converter
    // in its prior state and a retry would request the same expectedIn.
    // Net effect once committed: accumulator stays in [0, targetRate_), and
    // over K calls the cumulative input matches K * outputFrames *
    // sourceRate / targetRate exactly.
    const long long nextAccumulator{inputAccumulator_ + static_cast<long long>(outputFrames_) * sourceRate_ -
                                    static_cast<long long>(expectedIn) * static_cast<long long>(targetRate_)};

    // 1. Run soxr into the staging buffer. The staging buffer is sized so
    // soxr accepts the whole input block; verify that invariant at runtime
    // as a cheap guard against any future libsoxr version where the bound
    // is no longer tight - silently dropping input samples here would
    // degrade the output to carrier-only RF.
    const auto result{resampler_.value().process(in, std::span<float>{stagingBuffer_})};
    if (result == std::nullopt) {
        return AudioRateConverterStatus::Error;
    }
    if (result.value().inputConsumed != in.size()) {
        return AudioRateConverterStatus::Error;
    }
    // soxr accepted the full input span; safe to commit the accumulator now.
    inputAccumulator_ = nextAccumulator;
    const std::size_t produced{result.value().outputProduced};

    // 2. Queue the new samples behind the ones still waiting. The part of
    // the queue already handed out is dropped first, so the queue stays as
    // short as the backlog.
    if (pendingReadPos_ > 0) {
        pendingOutput_.erase(pendingOutput_.begin(), pendingOutput_.begin() + pendingReadPos_);
        pendingReadPos_ = 0;
    }
    pendingOutput_.insert(pendingOutput_.end(), stagingBuffer_.begin(), stagingBuffer_.begin() + produced);

    // 3. Hand out a block only when the queue fills it completely. soxr
    // delivers its output in bursts; padding a short block with silence
    // would put a gap into the middle of the audio.
    if (pendingOutput_.size() < out.size()) {
        return AudioRateConverterStatus::NeedsInput;
    }
    std::copy_n(pendingOutput_.begin(), out.size(), out.begin());
    pendingReadPos_ = out.size();
    return AudioRateConverterStatus::Ok;
}
