/**
 * @file polyphase_resampler.cpp
 * @brief Polyphase FIR resampler implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "polyphase_resampler.h"

#include <cassert>
#include <cmath>
#include <numbers>

namespace {
    /**
     * @brief Evaluate the prototype filter h[k] at index k.
     *
     * The prototype is a Hamming-windowed sinc, normalised so the sum of the
     * polyphase sub-filter sums to 1.0 after the +L scaling - i.e. unity DC
     * gain through the resampler. Centred at (totalTaps - 1) / 2 (an integer
     * for odd totalTaps, half-integer for even - both work because the sinc
     * is evaluated at the corresponding offset).
     *
     * @param k             Tap index, 0 <= k < totalTaps.
     * @param totalTaps     Total prototype filter length (== L * tapsPerPhase).
     * @param fcNormalised  Cutoff frequency normalised to the virtual rate
     *                      (cycles per virtual sample); must be in (0, 0.5).
     * @return Filter coefficient before polyphase decomposition.
     */
    [[nodiscard]] float evaluateProto(int k, int totalTaps, float fcNormalised) {
        const float center{static_cast<float>(totalTaps - 1) / 2.0f};
        const float m{static_cast<float>(k) - center};

        // Sinc at the cutoff: 2 * fc * sinc(2 * fc * m). The m == 0 branch
        // sidesteps the 0/0 form analytically (limit is 2 * fc).
        float h{};
        if (m == 0.0f) {
            h = 2.0f * fcNormalised;
        } else {
            const float arg{2.0f * std::numbers::pi_v<float> * fcNormalised * m};
            h = std::sin(arg) / (std::numbers::pi_v<float> * m);
        }

        // Hamming window: 0.54 - 0.46 * cos(2 pi k / (N - 1)). Trades a touch
        // of stop-band attenuation versus rectangular for sharply suppressed
        // ripple, which matters here because the resampler doubles as the
        // 15 kHz audio bandwidth guard for the FM MPX.
        const float window{0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(k) /
                                                    static_cast<float>(totalTaps - 1))};
        return h * window;
    }
}  // namespace

PolyphaseResampler::PolyphaseResampler(int interpL, int decimM, int tapsPerPhase, float cutoffHz, float sampleRateIn)
    : L_{interpL},
      M_{decimM},
      tapsPerPhase_{tapsPerPhase},
      coefs_(static_cast<std::size_t>(interpL) * static_cast<std::size_t>(tapsPerPhase), 0.0f),
      delay_(static_cast<std::size_t>(tapsPerPhase), 0.0f) {
    assert(interpL >= 1 && decimM >= 1 && tapsPerPhase >= 1);
    assert(cutoffHz > 0.0f && sampleRateIn > 0.0f);

    const int totalTaps{L_ * tapsPerPhase_};
    // Cutoff is specified at the input rate but the prototype operates at the
    // virtual L*Fs rate, so divide by L to get cycles per virtual sample.
    const float fcNormalised{cutoffHz / (static_cast<float>(L_) * sampleRateIn)};
    assert(fcNormalised > 0.0f && fcNormalised < 0.5f);

    // Decompose the prototype into L polyphase sub-filters. Each sub-filter
    // gets the +L scaling that compensates for the implicit zero-stuffing in
    // the polyphase decomposition (without it, every output is divided by L).
    //
    // Layout note: coefs_[phase][tap] = h_proto[tap * L + phase] is the
    // standard polyphase mapping. The +L scaling is folded in here so the
    // run-time inner product needs no per-output division.
    for (int phase{0}; phase < L_; ++phase) {
        for (int tap{0}; tap < tapsPerPhase_; ++tap) {
            const int k{tap * L_ + phase};
            coefs_[static_cast<std::size_t>(phase) * static_cast<std::size_t>(tapsPerPhase_) +
                   static_cast<std::size_t>(tap)] = evaluateProto(k, totalTaps, fcNormalised) * static_cast<float>(L_);
        }
    }
}

std::size_t PolyphaseResampler::outputSize(std::size_t inSize) const {
    return inSize * static_cast<std::size_t>(L_) / static_cast<std::size_t>(M_);
}

void PolyphaseResampler::pushDelay(float sample) {
    // Shift newest-first: delay_[0] is the most recent input, the rest slide
    // back by one. tapsPerPhase is small (typically 32) so the linear shift
    // is comparable to a circular buffer, with much simpler indexing in the
    // hot convolution loop.
    for (int i{tapsPerPhase_ - 1}; i > 0; --i) {
        delay_[static_cast<std::size_t>(i)] = delay_[static_cast<std::size_t>(i - 1)];
    }
    delay_[0] = sample;
}

float PolyphaseResampler::convolveAtPhase(int phase) const {
    const float* row{&coefs_[static_cast<std::size_t>(phase) * static_cast<std::size_t>(tapsPerPhase_)]};
    float acc{0.0f};
    for (int t{0}; t < tapsPerPhase_; ++t) {
        acc += row[t] * delay_[static_cast<std::size_t>(t)];
    }
    return acc;
}

void PolyphaseResampler::resample(std::span<const float> in, std::span<float> out) {
    assert(in.size() % static_cast<std::size_t>(M_) == 0);
    assert(out.size() == outputSize(in.size()));

    std::size_t outIdx{0};
    for (std::size_t i{0}; i < in.size(); ++i) {
        pushDelay(in[i]);

        // The smallest phase within this input that falls on a kept (mod-M)
        // virtual sample is the additive inverse of virtualPhase_ (mod M):
        // (phase + virtualPhase_) mod M == 0  <=>  phase mod M == (M - virtualPhase_) mod M.
        // From there we step by M, producing 4 or 5 outputs per input for the
        // 19/4 case, 19 outputs in total per 4 inputs.
        const int target{(M_ - virtualPhase_) % M_};
        for (int phase{target}; phase < L_; phase += M_) {
            out[outIdx++] = convolveAtPhase(phase);
        }
        virtualPhase_ = (virtualPhase_ + L_) % M_;
    }
}
