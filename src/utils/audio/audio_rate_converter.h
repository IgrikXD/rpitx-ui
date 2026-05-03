/**
 * @file audio_rate_converter.h
 * @brief Streaming sample-rate converter built on the SoxrResampler wrapper.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 29.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "soxr_resampler.h"

/**
 * @brief Single-channel streaming rate converter with a fixed output block contract.
 *
 * Wraps SoxrResampler so callers see a uniform fixed-size output block
 * regardless of the source / target rate ratio. The input block size varies
 * between calls via a Bresenham accumulator that tracks the rate ratio
 * exactly - the backend halts on either input exhaustion or output cap, so a
 * constant ceil()-rounded input with a tight output buffer
 * would drop samples or grow an internal spill unbounded. With Bresenham
 * sizing and an oversized staging buffer, libsoxr always consumes the full
 * input span and the per-call output jitter (one or two samples) is absorbed
 * by an internal spill.
 *
 * When sourceRateHz == targetRateHz no soxr instance is created and process()
 * degrades to a memory copy.
 *
 * Non-copyable; movable so the converter can be deferred-constructed inside
 * std::optional members (e.g. processors that learn the source rate at run
 * time).
 */
class AudioRateConverter {
public:
    /**
     * @brief Construct an AudioRateConverter for the given rate pair.
     *
     * @param sourceRateHz       Input sample rate in Hz (> 0).
     * @param targetRateHz       Output sample rate in Hz (> 0).
     * @param targetOutputFrames Output frames produced per process() call (> 0).
     *                           Sets the latency budget at the output rate;
     *                           in seconds this is targetOutputFrames /
     *                           targetRateHz.
     *
     * @throws std::invalid_argument when any parameter is non-positive or when
     *         the derived per-call input frame count overflows std::size_t.
     * @throws std::runtime_error    when the underlying soxr handle cannot be created.
     */
    AudioRateConverter(int sourceRateHz, int targetRateHz, std::size_t targetOutputFrames);

    AudioRateConverter(const AudioRateConverter&)            = delete;
    AudioRateConverter& operator=(const AudioRateConverter&) = delete;
    AudioRateConverter(AudioRateConverter&&)                 = default;
    AudioRateConverter& operator=(AudioRateConverter&&)      = default;

    /**
     * @brief Output frames produced per process() call (constant).
     */
    [[nodiscard]] std::size_t outputFrames() const noexcept {
        return outputFrames_;
    }

    /**
     * @brief Maximum input frames any single process() call may demand.
     *
     * Used by the pipeline to size source-read buffers conservatively.
     * Bresenham accumulator alternates between this value and one less.
     */
    [[nodiscard]] std::size_t maxInputFrames() const noexcept {
        return maxInputFrames_;
    }

    /**
     * @brief Input frames the next process() call expects.
     *
     * Pure: does not advance the Bresenham accumulator. Caller must read
     * exactly this many input frames from the source and pass them to the
     * matching process() call. After process(), the accumulator advances
     * and a fresh peek may return a different value.
     *
     * The value is by construction in [0, maxInputFrames()].
     */
    [[nodiscard]] std::size_t peekNextInputFrames() const noexcept {
        if (resampler_ == std::nullopt) {
            return outputFrames_;
        }
        // Bresenham invariant: inputAccumulator_ stays in [0, targetRate_)
        // and the cumulative input across K calls equals
        // K * outputFrames * sourceRate / targetRate, rounded to the
        // nearest integer at each step.
        const long long total{inputAccumulator_ + static_cast<long long>(outputFrames_) * sourceRate_};
        return static_cast<std::size_t>(total / targetRate_);
    }

    /**
     * @brief Process one block of audio.
     *
     * @param in  Input frames; size must equal peekNextInputFrames().
     * @param out Output frames; size must equal outputFrames().
     * @return true on success, false when the buffer geometry is invalid
     *         or soxr reports a processing error.
     */
    [[nodiscard]] bool process(std::span<const float> in, std::span<float> out);

    /**
     * @brief Reset filter state for a loop boundary.
     *
     * Empties the soxr filter delay line and drops any spilled output samples
     * so the end-of-file filter tail does not smear into the start of the
     * next iteration. The Bresenham accumulator is deliberately preserved
     * across the boundary: the loop wraps the source content but the
     * cumulative input/output rate ratio must keep tracking the target so
     * peekNextInputFrames() of the call that triggered this reset still
     * matches the input span the caller has already fetched, and longer
     * looped playbacks do not accumulate sample-count drift.
     */
    void reset() {
        spill_.clear();
        spillReadPos_ = 0;
        if (resampler_ != std::nullopt) {
            resampler_.value().clear();
        }
        // inputAccumulator_ deliberately preserved across the boundary.
    }

    /**
     * @brief Pull the converter's tail into out at end-of-stream.
     *
     * Drains spilled output samples first, then feeds soxr the documented
     * end-of-stream flush form (empty input span) to recover any samples
     * still buffered in the filter delay line.
     *
     * Once drain() has been called, the soxr instance is in libsoxr's
     * post-flush state - call reset() before resuming normal process() use.
     *
     * @param out Output buffer; size must not exceed outputFrames().
     * @return Number of frames written (0 .. out.size()) on success; 0 means
     *         the converter is fully drained. std::nullopt when libsoxr
     *         reports an error during the flush call - the caller should
     *         treat this as a hard pipeline failure rather than a clean end.
     */
    [[nodiscard]] std::optional<std::size_t> drain(std::span<float> out);

private:
    std::size_t outputFrames_;
    int sourceRate_;
    int targetRate_;
    std::size_t maxInputFrames_;
    long long inputAccumulator_;  ///< Bresenham state in [0, targetRate).

    std::optional<SoxrResampler> resampler_;  ///< nullopt = passthrough.

    /**
     * @brief Staging buffer for soxr's output writes.
     *
     * Sized to absorb the maximum number of output samples soxr can produce
     * from maxInputFrames_ inputs (plus a small safety margin). Letting
     * soxr fill a generous buffer guarantees idone == ilen on every call,
     * so no input is silently dropped at the boundary where output would
     * otherwise have filled first.
     */
    std::vector<float> stagingBuffer_;

    /**
     * @brief Pending output samples not yet returned to the caller.
     *
     * soxr's per-call output count jitters by one or two samples around
     * the rate ratio even with Bresenham input sizing; the surplus is
     * stashed here and drained at the head of the next process() call.
     * Stored as a flat vector with a separate read offset so we can
     * pop from the front in O(1) without a deque's per-element allocation.
     */
    std::vector<float> spill_;
    std::size_t spillReadPos_;
};
