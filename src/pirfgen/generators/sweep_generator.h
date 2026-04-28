/**
 * @file sweep_generator.h
 * @brief Linear frequency sweep generator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>

#include "rf_generator.h"
#include "sawtooth.h"

/**
 * @brief Thin wrapper around Sawtooth for RF frequency sweeping.
 *
 * Produces a linear ramp spanning [-bandwidth/2, +bandwidth/2] and wrapping at
 * the positive peak. Fully deterministic - no RNG, no hold state needed.
 */
class SweepGenerator : public RfGenerator {
public:
    /**
     * @brief Construct a sweep generator for the given RF bandwidth and DMA sample rate.
     * @param bandwidth Target RF bandwidth in Hz (amplitude of the ramp is bandwidth / 2).
     * @param sampleRate DMA sample rate in Hz.
     */
    SweepGenerator(float bandwidth, uint32_t sampleRate);

    [[nodiscard]] float nextSample() override;

private:
    /**
     * @brief Number of full sweep cycles per second.
     */
    static constexpr float RATE_HZ{1'000.0F};

    Sawtooth sawtooth_;  ///< Underlying frequency-offset ramp.
};
