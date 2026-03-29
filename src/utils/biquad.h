/**
 * @file biquad.h
 * @brief Second-order IIR (biquad) filter for audio signal processing.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <numbers>

/**
 * @brief Second-order IIR biquad filter (Direct Form I).
 *
 * Provides Butterworth high-pass and low-pass filter configurations.
 * Coefficients are pre-normalized by a0 during construction.
 *
 * @code
 * auto hpf{Biquad::highPass(300.0f, 48'000.0f)};
 * float filtered{hpf.process(sample)};
 * @endcode
 */
class Biquad {
public:
    /**
     * @brief Create a high-pass Butterworth biquad filter.
     * @param cutoffHz Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @return Configured high-pass Biquad instance.
     */
    [[nodiscard]] static Biquad highPass(float cutoffHz, float sampleRate);

    /**
     * @brief Create a low-pass Butterworth biquad filter.
     * @param cutoffHz Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @return Configured low-pass Biquad instance.
     */
    [[nodiscard]] static Biquad lowPass(float cutoffHz, float sampleRate);

    /**
     * @brief Process a single input sample through the filter.
     * @param in Input sample.
     * @return Filtered output sample.
     */
    [[nodiscard]] float process(float in);

private:
    /**
     * @brief Butterworth Q factor.
     */
    static constexpr float BUTTERWORTH_Q{1.0f / std::numbers::sqrt2_v<float>};

    float b0_{};  ///< Feedforward coefficient b0.
    float b1_{};  ///< Feedforward coefficient b1.
    float b2_{};  ///< Feedforward coefficient b2.
    float a1_{};  ///< Feedback coefficient a1.
    float a2_{};  ///< Feedback coefficient a2.
    float x1_{};  ///< Input delay z^-1.
    float x2_{};  ///< Input delay z^-2.
    float y1_{};  ///< Output delay z^-1.
    float y2_{};  ///< Output delay z^-2.
};
