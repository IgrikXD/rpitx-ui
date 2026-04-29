/**
 * @file soxr_resampler.h
 * @brief RAII C++ wrapper around the libsoxr streaming resampler.
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

/**
 * @brief Result of a single soxr_process() call.
 */
struct SoxrProcessResult {
    std::size_t inputConsumed;   ///< Frames soxr accepted from the input span.
    std::size_t outputProduced;  ///< Frames soxr wrote into the output span.
};

/**
 * @brief Single-channel float streaming resampler backed by libsoxr.
 *
 * Owns one soxr_t handle and is the only translation unit in the project that
 * pulls in soxr.h - downstream code (AudioRateConverter, AudioPipeline) sees a
 * pure C++ surface with std::span and std::optional. The handle is created in
 * the constructor at the requested rate pair and quality, and destroyed in the
 * destructor; non-copyable, movable so it can live in std::vector or be
 * returned from factories.
 *
 * Quality maps to soxr's published presets - High (~121 dB SNR) is the default
 * because it is plenty for the Pi's RF chain while keeping CPU and memory cost
 * far below the previous polyphase approach for non-rational rate ratios.
 */
class SoxrResampler {
public:
    /**
     * @brief Resampling quality preset.
     *
     * Mirrors libsoxr's SOXR_QQ / SOXR_LQ / SOXR_MQ / SOXR_HQ / SOXR_VHQ
     * recipes; see the libsoxr documentation for SNR / passband details.
     */
    enum class Quality {
        Quick,     ///< SOXR_QQ - cheapest, ~13 dB SNR (rarely useful).
        Low,       ///< SOXR_LQ - ~67 dB SNR.
        Medium,    ///< SOXR_MQ - ~96 dB SNR.
        High,      ///< SOXR_HQ - ~121 dB SNR; default for audio paths.
        VeryHigh,  ///< SOXR_VHQ - ~144 dB SNR.
    };

    /**
     * @brief Construct a single-channel resampler for the given rate pair.
     *
     * @param sourceRateHz Input sample rate in Hz (> 0).
     * @param targetRateHz Output sample rate in Hz (> 0).
     * @param quality      Quality preset; defaults to Medium (~96 dB SNR),
     *                     which exceeds the SNR of any FM/AM/SSB receiver
     *                     while keeping the filter length and per-block
     *                     CPU cost small enough for a Pi Zero.
     *
     * @throws std::invalid_argument when either rate is non-positive.
     * @throws std::runtime_error when libsoxr rejects the configuration.
     */
    SoxrResampler(int sourceRateHz, int targetRateHz, Quality quality = Quality::Medium);

    ~SoxrResampler();

    SoxrResampler(const SoxrResampler&)            = delete;
    SoxrResampler& operator=(const SoxrResampler&) = delete;

    SoxrResampler(SoxrResampler&& other) noexcept;
    SoxrResampler& operator=(SoxrResampler&& other) noexcept;

    /**
     * @brief Run one soxr_process() call.
     *
     * libsoxr returns when either the input is exhausted (idone == in.size)
     * or the output buffer is full (odone == out.size). The caller MUST
     * inspect inputConsumed - if it is less than in.size the surplus input
     * remains the caller's responsibility, and if the staging output buffer
     * is sized for the maximum output the rate ratio can yield, idone will
     * always equal in.size so no input is dropped.
     *
     * Passing an empty input span is libsoxr's documented end-of-stream
     * flush form; do NOT use it mid-stream because subsequent calls with
     * fresh input desynchronise the resampler's filter delay line and
     * collapse the output toward silence (the carrier-only RF pattern
     * observed when an earlier draft of this wrapper called the flush form
     * to top up partial blocks).
     *
     * @param in  Input frames (empty only at end-of-stream).
     * @param out Output buffer.
     * @return Frame counts on success; std::nullopt when libsoxr reports
     *         an error.
     */
    [[nodiscard]] std::optional<SoxrProcessResult> process(std::span<const float> in, std::span<float> out);

    /**
     * @brief Reset the internal filter delay line and phase.
     *
     * Wraps soxr_clear: leaves the configured rate / quality untouched but
     * empties any sample buffered inside the resampler. Use this at loop
     * boundaries so the filter tail of the previous file iteration does
     * not smear into the start of the next one.
     */
    void clear();

private:
    void* handle_;  ///< Opaque soxr_t; void* keeps soxr.h out of this header.
};
