/**
 * @file biquad.cpp
 * @brief Biquad filter implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "biquad.h"

#include <cmath>
#include <numbers>

namespace {

    /**
     * @brief Common RBJ-cookbook intermediate quantities for low/high-pass sections.
     */
    struct RbjParams {
        float cosW0;  ///< cos(omega_0).
        float alpha;  ///< sin(omega_0) / (2 * Q).
        float a0;     ///< Unnormalised feedback coefficient a0 = 1 + alpha.
    };

    /**
     * @brief Compute the shared RBJ biquad design quantities.
     * @param cutoffHz   Cutoff frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @param q          Pole-pair quality factor; must be strictly positive.
     * @return Filled RbjParams.
     */
    [[nodiscard]] RbjParams rbjParams(float cutoffHz, float sampleRate, float q) noexcept {
        const float w0{2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate};
        const float alpha{std::sin(w0) / (2.0f * q)};
        return {
            .cosW0 = std::cos(w0),
            .alpha = alpha,
            .a0    = 1.0f + alpha,
        };
    }

}  // namespace

Biquad Biquad::highPass(float cutoffHz, float sampleRate, float q) noexcept {
    const auto [cosW0, alpha, a0]{rbjParams(cutoffHz, sampleRate, q)};
    const float onePlusCosOverA0{(1.0f + cosW0) / a0};

    return Biquad{
        onePlusCosOverA0 / 2.0f,
        -onePlusCosOverA0,
        onePlusCosOverA0 / 2.0f,
        -2.0f * cosW0 / a0,
        (1.0f - alpha) / a0,
    };
}

Biquad Biquad::lowPass(float cutoffHz, float sampleRate, float q) noexcept {
    const auto [cosW0, alpha, a0]{rbjParams(cutoffHz, sampleRate, q)};
    const float oneMinusCosOverA0{(1.0f - cosW0) / a0};

    return Biquad{
        oneMinusCosOverA0 / 2.0f,
        oneMinusCosOverA0,
        oneMinusCosOverA0 / 2.0f,
        -2.0f * cosW0 / a0,
        (1.0f - alpha) / a0,
    };
}

Biquad Biquad::preEmphasis(float tauSeconds, float sampleRate, float boost) noexcept {
    // Bilinear transform of H(s) = (1 + s*tau_z) / (1 + s*tau_p) with
    //   tau_z = tauSeconds          (zero -> +6 dB/oct shelf onset)
    //   tau_p = tauSeconds / boost  (pole -> high-frequency plateau)
    // The resulting digital filter is first-order; it is laid out as a
    // Biquad with b2 = a2 = 0 so it shares the Direct-Form-I run-time path
    // with the second-order shelves. K = 2 * Fs is the standard bilinear
    // pre-warp factor; no further frequency warping is applied because
    // broadcast pre-emphasis is specified by time constant rather than by
    // a precise corner frequency.
    const float k{2.0f * sampleRate};
    const float tauP{tauSeconds / boost};
    const float a0{1.0f + k * tauP};

    return Biquad{
        (1.0f + k * tauSeconds) / a0,
        (1.0f - k * tauSeconds) / a0,
        0.0f,
        (1.0f - k * tauP) / a0,
        0.0f,
    };
}
