/**
 * @file pirfgen.h
 * @brief CLI/runtime declarations for the wideband RF generator transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cli_utils.h"
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
    inline constexpr uint32_t DEFAULT_SAMPLE_RATE{500'000};

    /**
     * @brief Maximum allowed multitone tone count.
     */
    inline constexpr int MAX_TONE_COUNT{1024};

    /**
     * @brief DMA drain fraction used to pace the refill loop.
     */
    inline constexpr float DMA_DRAIN_FRACTION{0.75F};

    /**
     * @brief RfGenMode textual names for CLI parsing and display.
     */
    inline constexpr std::array<NamedEnum<RfGenMode>, 3> MODE_TABLE{{
        NamedEnum<RfGenMode>{"noise", RfGenMode::Noise},
        NamedEnum<RfGenMode>{"sweep", RfGenMode::Sweep},
        NamedEnum<RfGenMode>{"multitone", RfGenMode::Multitone},
    }};

    /**
     * @brief RF generator parameters extracted from argv.
     */
    struct RfGenParameters {
        RfGenMode mode{RfGenMode::Noise};
        uint64_t freq{0};
        float bandwidth{0.0F};
        uint32_t sampleRate{DEFAULT_SAMPLE_RATE};
        std::optional<int> toneCount;  ///< Engaged iff -t was given on the CLI.
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     */
    void handleSignal(int sig);

    /**
     * @brief Print the command-line usage to stderr.
     */
    void printUsage();

    /**
     * @brief Parse and validate the two positional arguments (frequency, bandwidth).
     */
    [[nodiscard]] ParseResult parsePositionalArgs(std::string_view freqArg, std::string_view bwArg,
                                                  RfGenParameters& params);

    /**
     * @brief Walk the optional flag arguments and populate params.
     */
    [[nodiscard]] ParseResult parseOptionalFlags(std::span<char* const> args, RfGenParameters& params);

    /**
     * @brief Run cross-field validation after all CLI values have been collected.
     */
    [[nodiscard]] ParseResult validateOptions(const RfGenParameters& params);

    /**
     * @brief Parse and validate command-line arguments.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], RfGenParameters& params);

    /**
     * @brief Run the wideband RF generator transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pirfgen
