/**
 * @file hilbert.cpp
 * @brief Hilbert transform FIR filter implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "hilbert.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

    /**
     * @brief Validate the FIR tap count for the Hilbert transformer.
     * @param taps Requested number of taps.
     * @return @p taps unchanged on success.
     * @throws std::invalid_argument When @p taps is even or smaller than 3.
     */
    int validateTaps(int taps) {
        if (taps < 3 || taps % 2 == 0) {
            throw std::invalid_argument("Hilbert tap count must be odd and >= 3");
        }
        return taps;
    }

}  // namespace

Hilbert::Hilbert(int taps)
    : taps_{validateTaps(taps)},
      delay_{(taps_ - 1) / 2},
      coeffs_(taps_, 0.0f),
      hilbertLine_(taps_, 0.0f),
      delayLine_(taps_, 0.0f) {
    // Hilbert FIR coefficients with Blackman window. Even-indexed taps
    // (relative to the centre) are zero by definition of the ideal Hilbert
    // response, leaving only odd-indexed taps to carry the 2/(pi*k) kernel.
    constexpr float pi{std::numbers::pi_v<float>};
    const float denom{static_cast<float>(taps_ - 1)};

    for (int n{0}; n < taps_; ++n) {
        const int k{n - delay_};

        if ((k & 1) == 0) {
            continue;
        }

        const float w{0.42f - 0.5f * std::cos(2.0f * pi * n / denom) + 0.08f * std::cos(4.0f * pi * n / denom)};
        coeffs_[n] = (2.0f / (pi * k)) * w;
    }
}

IqSample Hilbert::process(float in) noexcept {
    hilbertLine_[pos_] = in;
    delayLine_[pos_]   = in;

    // FIR convolution for the Q channel (Hilbert-transformed).
    float qAcc{0.0f};
    int idx{pos_};
    for (int i{0}; i < taps_; ++i) {
        qAcc += coeffs_[i] * hilbertLine_[idx];
        if (--idx < 0) {
            idx = taps_ - 1;
        }
    }

    // I channel: delayed by delay_ samples to match the Hilbert group delay.
    idx = pos_ - delay_;
    if (idx < 0) {
        idx += taps_;
    }

    const IqSample result{
        .i = delayLine_[idx],
        .q = qAcc,
    };

    pos_ = (pos_ + 1) % taps_;

    return result;
}
