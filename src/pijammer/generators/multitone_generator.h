/**
 * @file multitone_generator.h
 * @brief Random fast-hopping multitone generator (FHSS-style).
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
#include <vector>

#include "jammer_generator.h"

/**
 * @brief Random fast-hopping across equidistant tones (FHSS-style).
 *
 * Pre-computes toneCount tone offsets centered in each bandwidth slot, then
 * hops between them at a fixed rate. Each hop picks a random tone, yielding
 * a discrete "comb" RF spectrum.
 */
class MultitoneGenerator : public JammerGenerator {
public:
    /**
     * @brief Construct a multitone generator for the given bandwidth, sample rate, and tone count.
     * @param bandwidth Target RF bandwidth in Hz.
     * @param sampleRate DMA sample rate in Hz.
     * @param toneCount Number of equidistant tones (must be >= 1).
     */
    MultitoneGenerator(float bandwidth, uint32_t sampleRate, int toneCount);

    [[nodiscard]] float nextSample() override;

private:
    /**
     * @brief Number of tone hops per second.
     */
    static constexpr float HOP_RATE_HZ{10'000.0F};

    /**
     * @brief Build a vector of equidistant tone offsets inside [-BW/2, +BW/2].
     * @param bandwidth Total occupied bandwidth in Hz.
     * @param toneCount Number of tones (must be >= 1).
     * @return Vector of tone frequency offsets in Hz.
     */
    [[nodiscard]] static std::vector<float> makeTones(float bandwidth, int toneCount);

    /**
     * @brief Compute the number of DMA samples to hold each tone between hops.
     * @param sampleRate DMA sample rate in Hz.
     * @return Hold duration in samples (clamped to >= 1).
     */
    [[nodiscard]] static int computeSamplesPerHop(uint32_t sampleRate);

    std::mt19937 engine_{std::random_device{}()};  ///< PRNG engine (per-instance).
    std::uniform_int_distribution<int> dist_;      ///< Uniform tone-index distribution.
    const std::vector<float> tones_;               ///< Pre-computed tone frequency offsets.
    const int samplesPerHop_;                      ///< DMA samples to hold each tone between hops.
    int counter_{0};                               ///< Samples elapsed within the current hop.
    int idx_;                                      ///< Index of the currently active tone.
};
