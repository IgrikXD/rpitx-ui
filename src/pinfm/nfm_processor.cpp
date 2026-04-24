/**
 * @file nfm_processor.cpp
 * @brief NBFM frequency-deviation processor implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 24.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "nfm_processor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    /**
     * @brief Per-section pole-pair Q of an N-th order Butterworth cascade.
     *
     * An N-th order Butterworth low-pass has its poles evenly spaced on a
     * semicircle of radius omega_c in the LHP. The standard cascade design
     * groups them into N/2 complex-conjugate pairs; the k-th pair sits at
     * angle theta_k = pi * (2k - 1) / (2N) measured from the negative real
     * axis (so theta close to 0 means a near-real pole, theta close to pi/2
     * means a near-imaginary pole), and the biquad section that realises
     * that pair has denominator Q given by Q_k = 1 / (2 * cos(theta_k)).
     * Cascading those sections at the same cutoff reproduces the full
     * N-th order Butterworth response (roll-off of 6*N dB/oct, maximally
     * flat in-band).
     *
     * Computed at runtime (std::cos is not constexpr in C++20) once per
     * processor instance, so the cost is negligible. The benefit over a
     * hard-coded table is that the formula is right here, transcription errors
     * are impossible, and bumping LPF_ORDER to 6/8 only requires appending
     * matching butterworthCascadeQ(order, k) entries to the ctor init list.
     *
     * @param order   Target Butterworth order N; must be even and >= 2.
     * @param section Section index k in [1, N/2].
     * @return Pole-pair Q for the given section.
     */
    [[nodiscard]] float butterworthCascadeQ(int order, int section) {
        const float theta{std::numbers::pi_v<float> * static_cast<float>(2 * section - 1) /
                          static_cast<float>(2 * order)};
        return 1.0f / (2.0f * std::cos(theta));
    }
}  // namespace

NfmProcessor::NfmProcessor(float sampleRate, float peakDeviation)
    : hpf_{Biquad::highPass(HPF_CUTOFF, sampleRate)},
      lpfChain_{
          Biquad::lowPass(LPF_CUTOFF, sampleRate, butterworthCascadeQ(LPF_ORDER, 1)),
          Biquad::lowPass(LPF_CUTOFF, sampleRate, butterworthCascadeQ(LPF_ORDER, 2)),
      },
      peakDeviation_{peakDeviation} {
    // Silent failure mode: extra slots in std::array<Biquad, LPF_ORDER / 2>
    // would value-init to zero-coefficient Biquads (inaudible output), not
    // real filter sections, so a mismatched init list above will not fail loudly.
    static_assert(LPF_ORDER == 4,
                  "lpfChain_ init list has two Biquad entries; bumping LPF_ORDER "
                  "requires appending matching butterworthCascadeQ(order, k) calls "
                  "and updating this static_assert.");
}

float NfmProcessor::process(float sample) {
    float filtered{hpf_.process(sample)};
    for (auto& section : lpfChain_) {
        filtered = section.process(filtered);
    }
    const float agcd{agc_.process(filtered)};

    // Hard-clamp at +-1 before scaling: the AGC can briefly overshoot on a
    // fast level jump (env_ still lagging), and an unclamped transient would
    // radiate as over-deviation, i.e. adjacent-channel splatter. Clamping in
    // normalised units lets peakDeviation_ be the single source of truth for
    // the output bound.
    const float clamped{std::clamp(agcd, -1.0f, 1.0f)};
    return clamped * peakDeviation_;
}
