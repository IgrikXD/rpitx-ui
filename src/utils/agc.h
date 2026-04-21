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
     * @brief Construct an AGC with the given configuration.
     * @param config AGC parameters.
     */
    explicit Agc(AgcConfig config);

    /**
     * @brief Apply AGC to an IQ sample pair.
     * @param sample Input IQ sample.
     * @return Gain-adjusted IQ sample.
     */
    [[nodiscard]] IqSample process(IqSample sample);

    /**
     * @brief Apply AGC to a real-valued audio sample.
     * @param sample Input scalar sample.
     * @return Gain-adjusted scalar sample.
     */
    [[nodiscard]] float process(float sample);

private:
    /**
     * @brief Update the envelope estimate with the given magnitude and return the gain to apply.
     * @param mag Current sample magnitude (|i+jq| for IQ, |x| for scalar).
     * @return Gain factor target / env, clamped to 1.0 when env is near zero.
     */
    [[nodiscard]] float updateGain(float mag);

    float target_;  ///< Target output amplitude.
    float attack_;  ///< Envelope attack coefficient.
    float decay_;   ///< Envelope decay coefficient.
    float env_;     ///< Current envelope estimate.
};
