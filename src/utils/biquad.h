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
 * Provides Butterworth high-pass and low-pass factories with a configurable
 * pole-pair Q. The Q parameter defaults to the 2nd-order Butterworth value
 * and can be overridden for use as one section of a cascaded higher-order
 * design (Chebyshev, Bessel, or staggered Butterworth poles). Coefficients
 * are pre-normalized by a0 during construction.
 *
 * @code
 * auto hpf{Biquad::highPass(300.0f, 48'000.0f)};                       // 2nd-order Butterworth
 * auto lpf{Biquad::lowPass(3'000.0f, 48'000.0f, 0.5412f)};             // section of a 4th-order cascade
 * float filtered{hpf.process(sample)};
 * @endcode
 */
class Biquad {
public:
    /**
     * @brief 2nd-order Butterworth pole-pair Q (1/sqrt(2)).
     *
     * Exposed so callers can keep the Butterworth default reference in sight
     * when constructing custom-Q cascades (e.g. "my section 1 Q vs. the
     * nominal Butterworth Q") without hard-coding 0.7071 at the call site.
     */
    static constexpr float BUTTERWORTH_Q{1.0f / std::numbers::sqrt2_v<float>};

    /**
     * @brief Create a high-pass biquad section.
     *
     * The pole-pair Q is the classical IIR biquad design parameter controlling
     * the transition-band behaviour; leave it at BUTTERWORTH_Q for a standalone
     * 2nd-order Butterworth, or pass the per-section Q from a higher-order
     * prototype to use this instance as one stage of a cascade.
     *
     * @param cutoffHz Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @param q Pole-pair quality factor; must be strictly positive. Defaults to BUTTERWORTH_Q.
     * @return Configured high-pass Biquad instance.
     */
    [[nodiscard]] static Biquad highPass(float cutoffHz, float sampleRate, float q = BUTTERWORTH_Q);

    /**
     * @brief Create a low-pass biquad section.
     *
     * @param cutoffHz Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @param q Pole-pair quality factor; must be strictly positive. Defaults to BUTTERWORTH_Q.
     * @return Configured low-pass Biquad instance.
     */
    [[nodiscard]] static Biquad lowPass(float cutoffHz, float sampleRate, float q = BUTTERWORTH_Q);

    /**
     * @brief Process a single input sample through the filter.
     * @param in Input sample.
     * @return Filtered output sample.
     */
    [[nodiscard]] float process(float in);

private:
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
