/**
 * @file pirfgen.h
 * @brief CLI/runtime declarations for the wideband RF generator transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "cli_parse_result.h"
#include "rfgen_processor.h"

namespace pirfgen {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr int DMA_FIFO_SIZE{4096};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    inline constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief Default DMA sample rate in Hz.
     */
    inline constexpr int DEFAULT_SAMPLE_RATE{500'000};

    /**
     * @brief Maximum allowed multitone tone count.
     */
    inline constexpr int MAX_TONE_COUNT{1024};

    /**
     * @brief DMA drain fraction used to pace the refill loop.
     */
    inline constexpr float DMA_DRAIN_FRACTION{0.75F};

    /**
     * @brief RF generator parameters extracted from argv.
     */
    struct RfGenParameters {
        RfGenMode mode{RfGenMode::Noise};
        uint64_t transmissionFrequency{0};
        float bandwidth{0.0F};
        int sampleRate{DEFAULT_SAMPLE_RATE};
        std::optional<int> toneCount;  ///< Engaged iff --tone-count was given on the CLI.
    };

    /**
     * @brief Display name for an RfGenMode (matches the --mode CLI value).
     */
    [[nodiscard]] const char* modeName(RfGenMode mode);

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], RfGenParameters& params);

    /**
     * @brief Run the wideband RF generator transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pirfgen
