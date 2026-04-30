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
#include <memory>
#include <optional>
#include <span>

/**
 * @brief Result of a single resampling step.
 */
struct SoxrProcessResult {
    std::size_t inputConsumed;   ///< Frames accepted from the input span.
    std::size_t outputProduced;  ///< Frames written into the output span.
};

/**
 * @brief Single-channel float streaming resampler backed by libsoxr.
 *
 * Exposes a C++ surface with std::span and std::optional while hiding the
 * libsoxr C handle behind PIMPL. Non-copyable, movable so it can live in
 * std::vector or be returned from factories.
 */
class SoxrResampler {
public:
    /**
     * @brief Resampling quality preset.
     *
     * Mirrors libsoxr's quality recipes; see the libsoxr documentation for
     * SNR / passband details.
     */
    enum class Quality {
        Quick,     ///< Cheapest, ~13 dB SNR (rarely useful).
        Low,       ///< ~67 dB SNR.
        Medium,    ///< ~96 dB SNR; default for audio paths.
        High,      ///< ~121 dB SNR.
        VeryHigh,  ///< ~144 dB SNR.
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
     * @throws std::runtime_error    when libsoxr rejects the configuration.
     */
    SoxrResampler(int sourceRateHz, int targetRateHz, Quality quality = Quality::Medium);

    ~SoxrResampler();

    SoxrResampler(const SoxrResampler&)            = delete;
    SoxrResampler& operator=(const SoxrResampler&) = delete;

    SoxrResampler(SoxrResampler&& other) noexcept;
    SoxrResampler& operator=(SoxrResampler&& other) noexcept;

    /**
     * @brief Run one streaming resampling step.
     *
     * The backend returns when either the input is exhausted
     * (inputConsumed == in.size) or the output buffer is full
     * (outputProduced == out.size). The caller MUST inspect inputConsumed - if
     * it is less than in.size the surplus input remains the caller's
     * responsibility; sizing the staging output buffer for the maximum output
     * the rate ratio can yield guarantees inputConsumed == in.size so no input
     * is dropped.
     *
     * Passing an empty input span is libsoxr's documented end-of-stream
     * flush form; do NOT use it mid-stream because subsequent calls with
     * fresh input desynchronise the resampler's filter delay line and
     * collapse the output toward silence.
     *
     * @param in  Input frames (empty only at end-of-stream).
     * @param out Output buffer.
     * @return Frame counts on success; std::nullopt when libsoxr reports
     *         an error.
     */
    [[nodiscard]] std::optional<SoxrProcessResult> process(std::span<const float> in, std::span<float> out) noexcept;

    /**
     * @brief Reset the internal filter delay line and phase.
     *
     * Leaves the configured rate / quality untouched but empties any sample
     * buffered inside the resampler. Use this at loop boundaries so the filter
     * tail of the previous file iteration does not smear into the start of the
     * next one.
     */
    void clear() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
