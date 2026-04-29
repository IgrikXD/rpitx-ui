/**
 * @file pissb.h
 * @brief CLI/runtime declarations for the SSB transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <string>

#include "cli_parse_result.h"
#include "ssb_processor.h"

namespace pissb {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{4096};

    /**
     * @brief DMA time-register precision in bits (matches sendiq / other rpitx modules).
     */
    inline constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief Internal SSB audio / IQ sample rate in Hz.
     */
    inline constexpr int TARGET_SAMPLE_RATE{48'000};

    /**
     * @brief Target output frames per processing block (~21 ms at 48 kHz).
     */
    inline constexpr int TARGET_OUTPUT_FRAMES{1024};

    /**
     * @brief Minimum accepted input sample rate in Hz.
     */
    inline constexpr int MIN_INPUT_RATE{8'000};

    /**
     * @brief Maximum accepted input sample rate in Hz.
     */
    inline constexpr int MAX_INPUT_RATE{192'000};

    /**
     * @brief Default harmonic passed to iqdmasync.
     */
    inline constexpr int DEFAULT_HARMONIC{1};

    /**
     * @brief SSB parameters extracted from argv.
     */
    struct PissbParameters {
        uint64_t transmissionFrequency{0};
        std::string audioPath{""};
        bool loop{false};
        SsbMode mode{SsbMode::USB};
    };

    /**
     * @brief Display name for an SsbMode (matches the --sideband CLI value).
     */
    [[nodiscard]] const char* modeName(SsbMode mode);

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PissbParameters& params);

    /**
     * @brief Run the SSB transmitter command.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pissb
