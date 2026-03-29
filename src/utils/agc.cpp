/**
 * @file agc.cpp
 * @brief AGC implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "agc.h"

#include <cmath>

Agc::Agc(AgcConfig config)
    : target_{config.target}, attack_{config.attack}, decay_{config.decay}, env_{config.initialEnvelope} {
}

IqSample Agc::process(IqSample sample) {
    // Envelope tracking with asymmetric attack/decay
    const float mag{std::hypot(sample.i, sample.q)};
    if (mag > env_) {
        env_ += attack_ * (mag - env_);
    } else {
        env_ += decay_ * (mag - env_);
    }

    const float gain{(env_ > 1e-6f) ? target_ / env_ : 1.0f};

    return {
        .i = sample.i * gain,
        .q = sample.q * gain,
    };
}
