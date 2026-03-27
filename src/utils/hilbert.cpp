/**
 * @file hilbert.cpp
 * @brief Hilbert transform FIR filter implementation.
 */

#include "hilbert.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

static int validateTaps(int taps) {
    if (taps < 3 || taps % 2 == 0) {
        throw std::invalid_argument("Hilbert tap count must be odd and >= 3");
    }
    return taps;
}

Hilbert::Hilbert(int taps)
    : taps_{validateTaps(taps)},
      delay_{(taps - 1) / 2},
      coeffs_(taps, 0.0f),
      hilbertLine_(taps, 0.0f),
      delayLine_(taps, 0.0f) {
    // Compute Hilbert FIR coefficients with Blackman window
    for (int n{0}; n < taps_; ++n) {
        const int k{n - delay_};

        if (k == 0 || (k & 1) == 0) {
            coeffs_[n] = 0.0f;
        } else {
            const float w{0.42f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * n / (taps_ - 1)) +
                          0.08f * std::cos(4.0f * std::numbers::pi_v<float> * n / (taps_ - 1))};
            coeffs_[n] = (2.0f / (std::numbers::pi_v<float> * k)) * w;
        }
    }
}

int Hilbert::delay() const {
    return delay_;
}

int Hilbert::taps() const {
    return taps_;
}

IqSample Hilbert::process(float in) {
    hilbertLine_[pos_] = in;
    delayLine_[pos_]   = in;

    // FIR convolution for Q channel (Hilbert-transformed)
    float qAcc{0.0f};
    int idx{pos_};
    for (int i{0}; i < taps_; ++i) {
        qAcc += coeffs_[i] * hilbertLine_[idx];
        if (--idx < 0) {
            idx = taps_ - 1;
        }
    }

    // I channel: delayed by delay_ samples to match Hilbert group delay
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
