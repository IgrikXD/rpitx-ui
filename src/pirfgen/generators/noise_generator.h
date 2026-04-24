/**
 * @file noise_generator.h
 * @brief Band-limited uniform pseudo-random noise generator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <random>

#include "rf_generator.h"

/**
 * @brief Uniform pseudo-random noise with sample-and-hold band-limiting.
 *
 * Each random value is held for a fixed number of DMA samples before
 * regenerating. This band-limits the modulating noise so that, by Carson's
 * rule, the RF spectrum stays close to the target rectangular profile:
 * B_RF ~= bandwidth * (1 + 2 * MOD_BW_FRACTION). Updating every DMA sample
 * would instead spread the spectrum across the full sample rate.
 */
class NoiseGenerator : public RfGenerator {
public:
    /**
     * @brief Construct a noise generator for the given RF bandwidth and DMA sample rate.
     * @param bandwidth Target RF bandwidth in Hz.
     * @param sampleRate DMA sample rate in Hz.
     */
    NoiseGenerator(float bandwidth, uint32_t sampleRate);

    [[nodiscard]] float nextSample() override;

private:
    /**
     * @brief Modulation bandwidth as a fraction of the target RF bandwidth.
     */
    static constexpr float MOD_BW_FRACTION{0.125F};

    /**
     * @brief Compute the number of DMA samples to hold each random value.
     * @param bandwidth Target RF bandwidth in Hz.
     * @param sampleRate DMA sample rate in Hz.
     * @return Hold duration in samples (clamped to >= 1).
     */
    [[nodiscard]] static int computeHoldSamples(float bandwidth, uint32_t sampleRate);

    std::mt19937 engine_{std::random_device{}()};  ///< PRNG engine (per-instance).
    std::uniform_real_distribution<float> dist_;   ///< Uniform [-BW/2, +BW/2] distribution.
    const int holdSamples_;                        ///< DMA samples to hold each random value.
    int counter_{0};                               ///< Samples elapsed within the current held value.
    float current_;                                ///< Currently held frequency offset in Hz.
};
