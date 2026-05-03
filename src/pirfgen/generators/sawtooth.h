/**
 * @file sawtooth.h
 * @brief Sawtooth (linear ramp) oscillator primitive.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

/**
 * @brief Linear sawtooth (ramp) oscillator.
 *
 * Produces a monotonically increasing signal in the range [-amplitude, +amplitude)
 * that wraps back to -amplitude once the positive peak is reached.
 *
 * @code
 * Sawtooth sawtooth{100.0f, 500};
 * const float offsetHz{sawtooth.nextSample()};
 * @endcode
 */
class Sawtooth {
public:
    /**
     * @brief Construct a sawtooth oscillator.
     * @param amplitude Peak absolute value; signal ranges in [-amplitude, +amplitude).
     * @param samplesPerCycle Number of samples in one full ramp cycle (clamped to >= 1).
     */
    Sawtooth(float amplitude, int samplesPerCycle);

    /**
     * @brief Advance by one sample and return the current value.
     * @return Signal value in [-amplitude, +amplitude).
     */
    [[nodiscard]] float nextSample();

private:
    float amplitude_;  ///< Peak absolute value.
    float step_;       ///< Per-sample position increment.
    float pos_;        ///< Current position in the ramp.
};
