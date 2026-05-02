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
    [[nodiscard]] static Biquad highPass(float cutoffHz, float sampleRate, float q = BUTTERWORTH_Q) noexcept;

    /**
     * @brief Create a low-pass biquad section.
     *
     * @param cutoffHz Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @param q Pole-pair quality factor; must be strictly positive. Defaults to BUTTERWORTH_Q.
     * @return Configured low-pass Biquad instance.
     */
    [[nodiscard]] static Biquad lowPass(float cutoffHz, float sampleRate, float q = BUTTERWORTH_Q) noexcept;

    /**
     * @brief Create a first-order FM pre-emphasis filter as a Biquad.
     *
     * Implements the broadcast-FM pre-emphasis transfer function
     * H(s) = (1 + s*tau) / (1 + s*tau / boost), bilinear-transformed to
     * the digital domain. The shelf rises at +6 dB/oct from f_low =
     * 1/(2 pi tau) up to a high-frequency plateau at boost (in linear
     * units), so the audio is amplified above the corner frequency before
     * being radiated and the matching de-emphasis at the receiver restores
     * a flat response while attenuating the FM detector noise floor.
     *
     * Time-constant convention:
     *   - 50 us: ITU regions 1 / 3 (Europe, Africa, Asia, Oceania)
     *   - 75 us: ITU region 2 (Americas) and Japan
     *
     * The high-frequency plateau is configurable but defaults to a typical
     * commercial-exciter value (boost = 16, ~24 dB) - large enough that the
     * upper-knee corner (boost / (2 pi tau)) sits well above the 15 kHz
     * audio mask, so within the audio band the shelf is still rising at
     * +6 dB/oct rather than already plateauing. Realised as a first-order
     * IIR (b2 = a2 = 0) so it shares Biquad's Direct-Form-I run-time path
     * and per-sample state without paying for a second-order's storage.
     *
     * @param tauSeconds Pre-emphasis time constant (5e-5 or 7.5e-5).
     * @param sampleRate Sample rate in Hz.
     * @param boost      Linear high-frequency plateau gain; must be > 1.
     * @return Configured pre-emphasis Biquad instance.
     */
    [[nodiscard]] static Biquad preEmphasis(float tauSeconds, float sampleRate, float boost = 16.0f) noexcept;

    /**
     * @brief Process a single input sample through the filter.
     * @param in Input sample.
     * @return Filtered output sample.
     */
    [[nodiscard]] float process(float in) noexcept {
        const float out{b0_ * in + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_};

        x2_ = x1_;
        x1_ = in;
        y2_ = y1_;
        y1_ = out;

        return out;
    }

private:
    Biquad(float b0, float b1, float b2, float a1, float a2) noexcept : b0_{b0}, b1_{b1}, b2_{b2}, a1_{a1}, a2_{a2} {
    }

    float b0_;    ///< Feedforward coefficient b0.
    float b1_;    ///< Feedforward coefficient b1.
    float b2_;    ///< Feedforward coefficient b2.
    float a1_;    ///< Feedback coefficient a1.
    float a2_;    ///< Feedback coefficient a2.
    float x1_{};  ///< Input delay z^-1.
    float x2_{};  ///< Input delay z^-2.
    float y1_{};  ///< Output delay z^-1.
    float y2_{};  ///< Output delay z^-2.
};
