/**
 * @file biquad.cpp
 * @brief Biquad filter implementation.
 */

#include "biquad.h"

#include <cmath>
#include <numbers>

Biquad Biquad::highPass(float cutoffHz, float sampleRate) {
    const float w0{2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate};
    const float alpha{std::sin(w0) / (2.0f * BUTTERWORTH_Q)};
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

Biquad Biquad::lowPass(float cutoffHz, float sampleRate) {
    const float w0{2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate};
    const float alpha{std::sin(w0) / (2.0f * BUTTERWORTH_Q)};
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

float Biquad::process(float in) {
    const float out{b0_ * in + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_};

    x2_ = x1_;
    x1_ = in;
    y2_ = y1_;
    y1_ = out;

    return out;
}
