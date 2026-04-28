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

Biquad Biquad::highPass(float cutoffHz, float sampleRate, float q) {
    const float w0{2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate};
    const float alpha{std::sin(w0) / (2.0f * q)};
    const float cosW0{std::cos(w0)};
    const float a0{1.0f + alpha};

    Biquad bq{};
    bq.b0_ = (1.0f + cosW0) / 2.0f / a0;
    bq.b1_ = -(1.0f + cosW0) / a0;
    bq.b2_ = (1.0f + cosW0) / 2.0f / a0;
    bq.a1_ = -2.0f * cosW0 / a0;
    bq.a2_ = (1.0f - alpha) / a0;

    return bq;
}

Biquad Biquad::lowPass(float cutoffHz, float sampleRate, float q) {
    const float w0{2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate};
    const float alpha{std::sin(w0) / (2.0f * q)};
    const float cosW0{std::cos(w0)};
    const float a0{1.0f + alpha};

    Biquad bq{};
    bq.b0_ = (1.0f - cosW0) / 2.0f / a0;
    bq.b1_ = (1.0f - cosW0) / a0;
    bq.b2_ = (1.0f - cosW0) / 2.0f / a0;
    bq.a1_ = -2.0f * cosW0 / a0;
    bq.a2_ = (1.0f - alpha) / a0;

    return bq;
}

Biquad Biquad::preEmphasis(float tauSeconds, float sampleRate, float boost) {
    // Bilinear transform of H(s) = (1 + s*tau_z) / (1 + s*tau_p) with
    //   tau_z = tauSeconds                  (zero -> +6 dB/oct shelf onset)
    //   tau_p = tauSeconds / boost          (pole -> high-frequency plateau)
    // The resulting digital filter is first-order; we lay it out as a
    // Biquad with b2 = a2 = 0 so it shares the Direct-Form-I run-time
    // path with the second-order shelves. K = 2 * Fs is the standard
    // bilinear pre-warp factor; no further frequency warping is applied
    // because broadcast pre-emphasis is specified by time constant
    // rather than by a precise corner frequency.
    const float k{2.0f * sampleRate};
    const float tauZ{tauSeconds};
    const float tauP{tauSeconds / boost};
    const float a0{1.0f + k * tauP};

    Biquad bq{};
    bq.b0_ = (1.0f + k * tauZ) / a0;
    bq.b1_ = (1.0f - k * tauZ) / a0;
    bq.b2_ = 0.0f;
    bq.a1_ = (1.0f - k * tauP) / a0;
    bq.a2_ = 0.0f;

    return bq;
}

float Biquad::process(float in) {
    const float out{b0_ * in + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_};

    x2_ = x1_;
    x1_ = in;
    y2_ = y1_;
    y1_ = out;

    return out;
}
