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
     * @param quality      Quality preset; defaults to High.
     *
     * @throws std::invalid_argument when either rate is non-positive.
     * @throws std::runtime_error when libsoxr rejects the configuration.
     */
    SoxrResampler(int sourceRateHz, int targetRateHz, Quality quality = Quality::High);

    ~SoxrResampler();

    SoxrResampler(const SoxrResampler&)            = delete;
    SoxrResampler& operator=(const SoxrResampler&) = delete;

    SoxrResampler(SoxrResampler&& other) noexcept;
    SoxrResampler& operator=(SoxrResampler&& other) noexcept;

    /**
     * @brief Run one soxr_process() call.
     *
     * libsoxr consumes input and produces output asynchronously: it always
     * accepts the entire input span (so the caller never has to retry the
     * same data), but it caps output at out.size() and may produce fewer
     * frames than requested when the internal filter has not yet warmed up
     * or when the input is too small for the rate ratio. Pass an empty
     * input span to flush samples buffered in the filter delay line.
     *
     * @param in  Input frames (may be empty for a flush call).
     * @param out Output buffer.
     * @return Frame counts on success; std::nullopt when libsoxr reports
     *         an error.
     */
    [[nodiscard]] std::optional<SoxrProcessResult> process(std::span<const float> in, std::span<float> out);

private:
    void* handle_;  ///< Opaque soxr_t; void* keeps soxr.h out of this header.
};
