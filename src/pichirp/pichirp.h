/**
 * @file pichirp.h
 * @brief CLI/runtime declarations for the sinusoidal FM chirp transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>

#include "cli_parse_result.h"

namespace pichirp {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr int DMA_FIFO_SIZE{4096};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    inline constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief DMA sample rate in Hz.
     */
    inline constexpr uint32_t SAMPLE_RATE{200'000};

    /**
     * @brief DMA drain fraction used to pace the refill loop.
     */
    inline constexpr float DMA_DRAIN_FRACTION{0.75F};

    /**
     * @brief Minimum number of samples per sweep cycle.
     */
    inline constexpr int MIN_PERIOD_SAMPLES{100};

    /**
     * @brief Maximum number of samples per sweep cycle.
     */
    inline constexpr int MAX_PERIOD_SAMPLES{1'000'000'000};

    /**
     * @brief Chirp parameters extracted from argv.
     */
    struct ChirpParameters {
        uint64_t transmissionFrequency{0};
        float bandwidth{0.0F};
        float sweepTime{0.0F};
        int periodSamples{0};  ///< Derived: sweepTime * SAMPLE_RATE, validated in parseArgs.
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], ChirpParameters& params);

    /**
     * @brief Run the sinusoidal FM chirp transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pichirp
