/**
 * @file agc.h
 * @brief Fast automatic gain control (AGC) for IQ and real-valued audio signals.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cmath>

#include "iq_sample.h"

/**
 * @brief Configuration parameters for the AGC.
 *
 * All fields must be explicitly provided - no domain-specific defaults.
 *
 * @code
 * Agc agc{{.target = 0.8f, .attack = 0.1f, .decay = 0.001f, .initialEnvelope = 1e-4f}};
 * @endcode
 */
struct AgcConfig {
    float target;           ///< Target output amplitude.
    float attack;           ///< Envelope attack coefficient (fast response to level increase).
    float decay;            ///< Envelope decay coefficient (slow response to level decrease).
    float initialEnvelope;  ///< Initial envelope estimate.
};

/**
 * @brief Fast envelope-tracking AGC for IQ sample pairs or scalar audio samples.
 *
 * Tracks the signal envelope with asymmetric attack/decay smoothing
 * and applies gain to maintain a constant output amplitude. The internal
 * envelope state is shared between the IQ and scalar overloads, so a
 * single instance is intended to be used for one signal domain at a time.
 *
 * @code
 * Agc agc{{.target = 0.8f, .attack = 0.1f, .decay = 0.001f, .initialEnvelope = 1e-4f}};
 * auto normalized{agc.process(iq)};
 * @endcode
 */
class Agc {
public:
    /**
     * @brief Envelope magnitude below which updateGain() clamps to unity instead of dividing
     *        by the near-zero envelope, keeping the gain finite when no signal is present.
     */
    static constexpr float ENVELOPE_FLOOR{1e-6F};

    /**
     * @brief Construct an AGC with the given configuration.
     * @param config AGC parameters.
     */
    explicit Agc(AgcConfig config) noexcept
        : target_{config.target}, attack_{config.attack}, decay_{config.decay}, env_{config.initialEnvelope} {
    }

    /**
     * @brief Apply AGC to an IQ sample pair.
     * @param sample Input IQ sample.
     * @return Gain-adjusted IQ sample.
     */
    [[nodiscard]] IqSample process(IqSample sample) noexcept {
        const float gain{updateGain(std::hypot(sample.i, sample.q))};
        return {
            .i = sample.i * gain,
            .q = sample.q * gain,
        };
    }

    /**
     * @brief Apply AGC to a real-valued audio sample.
     * @param sample Input scalar sample.
     * @return Gain-adjusted scalar sample.
     */
    [[nodiscard]] float process(float sample) noexcept {
        return sample * updateGain(std::abs(sample));
    }

    /**
     * @brief Advance the envelope estimate from an externally-computed
     *        magnitude and return the gain to apply.
     *
     * Use this when a single shared gain must be applied to several
     * correlated channels (e.g. stereo L / R driven from max(|L|, |R|)
     * so the inter-channel level relationship is preserved). Each call
     * advances the envelope state exactly as the process() overloads do,
     * so do not mix updateGain() and process() calls on the same instance
     * within one sample step.
     *
     * @param mag Current sample magnitude (|i+jq| for IQ, |x| for scalar,
     *            max(|L|, |R|) for shared-gain stereo). Must be >= 0.
     * @return Gain factor target / env, clamped to 1.0 when env is near zero.
     */
    [[nodiscard]] float updateGain(float mag) noexcept {
        // Asymmetric attack/decay envelope tracker.
        if (mag > env_) {
            env_ += attack_ * (mag - env_);
        } else {
            env_ += decay_ * (mag - env_);
        }

        if (env_ > ENVELOPE_FLOOR) {
            return target_ / env_;
        }
        return 1.0f;
    }

private:
    float target_;  ///< Target output amplitude.
    float attack_;  ///< Envelope attack coefficient.
    float decay_;   ///< Envelope decay coefficient.
    float env_;     ///< Current envelope estimate.
};
