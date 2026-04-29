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
 * Wraps SoxrResampler so callers see a uniform fixed-size output block regardless
 * of the source / target rate ratio. The input block size varies between calls
 * via a Bresenham accumulator so the cumulative input rate matches the target
 * ratio exactly - critical because libsoxr halts soxr_process() as soon as
 * either the input is exhausted OR the output buffer is full, and feeding a
 * constant ceil()-rounded input together with a tight output buffer would
 * either drop input samples or grow an internal spill without bound on long
 * playbacks. With Bresenham input sizing plus an oversized staging buffer for
 * soxr's writes, libsoxr always consumes the full input span and the residual
 * jitter (one or two samples per call) is absorbed by an internal output spill.
 *
 * When sourceRateHz == targetRateHz, no soxr instance is created and process()
 * degrades to a memory copy, mirroring the previous polyphase-based behaviour.
 *
 * Non-copyable; movable so the converter can be assigned into std::optional
 * members in deferred-construction patterns (e.g. processors that learn the
 * source rate at run time).
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
     * @throws std::invalid_argument when any parameter is non-positive.
     * @throws std::runtime_error when the underlying soxr handle cannot be created.
     */
    AudioRateConverter(int sourceRateHz, int targetRateHz, int targetOutputFrames);

    AudioRateConverter(const AudioRateConverter&)            = delete;
    AudioRateConverter& operator=(const AudioRateConverter&) = delete;
    AudioRateConverter(AudioRateConverter&&)                 = default;
    AudioRateConverter& operator=(AudioRateConverter&&)      = default;

    /**
     * @brief Output frames produced per process() call (constant).
     */
    [[nodiscard]] int outputFrames() const;

    /**
     * @brief Maximum input frames any single process() call may demand.
     *
     * Used by the pipeline to size source-read buffers conservatively.
     * Bresenham accumulator alternates between this value and one less.
     */
    [[nodiscard]] int maxInputFrames() const;

    /**
     * @brief Input frames the next process() call expects.
     *
     * Pure: does not advance the Bresenham accumulator. Caller must read
     * exactly this many input frames from the source and pass them to the
     * matching process() call. After process(), the accumulator advances
     * and a fresh peek may return a different value.
     */
    [[nodiscard]] int peekNextInputFrames() const;

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
    void reset();

    /**
     * @brief Pull the converter's tail into out at end-of-stream.
     *
     * Drains spilled output samples first, then feeds soxr the documented
     * end-of-stream flush form (empty input span) to recover any samples
     * still buffered in the filter delay line. Returns the number of frames
     * written into out; a return value of 0 means the converter is fully
     * drained and the pipeline should report End to its caller.
     *
     * Once drain() has been called, the soxr instance is in libsoxr's
     * post-flush state - call reset() before resuming normal process() use.
     *
     * @param out Output buffer; size must not exceed outputFrames().
     * @return Number of frames written (0 .. out.size()).
     */
    [[nodiscard]] std::size_t drain(std::span<float> out);

private:
    int outputFrames_;
    int sourceRate_;
    int targetRate_;
    int maxInputFrames_;
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
