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

#include "soxr_resampler.h"

/**
 * @brief Single-channel streaming rate converter with a fixed block contract.
 *
 * Wraps SoxrResampler so that callers see a uniform fixed-size block API
 * regardless of the source/target rate ratio - the input span size and the
 * output span size are both decided at construction time and stay constant
 * across process() calls. When sourceRate == targetRate, no soxr instance
 * is created and process() degrades to a memory copy, matching the previous
 * polyphase-based behaviour.
 *
 * Block-size policy:
 *   - outputFrames_ is exactly the targetOutputFrames requested by the
 *     caller (drives the latency budget at the output rate).
 *   - inputFrames_ is ceil(targetOutputFrames * sourceRate / targetRate)
 *     so each call hands soxr at least as many inputs as it needs to fill
 *     the requested outputs in steady state. soxr buffers any unconsumed
 *     surplus internally, so over-feeding is harmless: the small per-call
 *     surplus shows up as a sub-permille effective rate offset (well
 *     below the Pi DMA clock tolerance and inaudible to listeners).
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
     * @brief Required input frames per process() call.
     */
    [[nodiscard]] int inputFrames() const;

    /**
     * @brief Output frames produced per process() call.
     */
    [[nodiscard]] int outputFrames() const;

    /**
     * @brief Process one block of audio.
     *
     * In resample mode, runs the input through soxr and zero-pads the tail
     * if soxr underproduces during the very first calls (filter warmup).
     * Subsequent calls reach steady state and fill out completely. In
     * passthrough mode, copies in -> out (sizes must match by contract).
     *
     * @param in  Input frames; size must equal inputFrames().
     * @param out Output frames; size must equal outputFrames().
     * @return true on success, false when the buffer geometry is invalid
     *         or soxr reports a processing error.
     */
    [[nodiscard]] bool process(std::span<const float> in, std::span<float> out);

private:
    int inputFrames_;
    int outputFrames_;
    std::optional<SoxrResampler> resampler_;  ///< nullopt = passthrough.
};
