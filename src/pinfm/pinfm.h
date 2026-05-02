/**
 * @file pinfm.h
 * @brief CLI/runtime declarations for the narrow-band FM transmitter.
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

namespace pinfm {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{8192};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    inline constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief Internal NBFM processing rate in Hz (also the DMA rate).
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
     * @brief NBFM deviation preset mode.
     */
    enum class NfmMode : uint8_t {
        Narrow,
        Wide,
    };

    /**
     * @brief NFM parameters extracted from argv.
     */
    struct NfmParameters {
        uint64_t transmissionFrequency{0};
        std::string audioPath{""};
        bool useStdin{false};
        bool loop{false};
        NfmMode mode{NfmMode::Wide};  ///< Default to wide (+-5 kHz, amateur VHF/UHF).
    };

    /**
     * @brief Peak deviation (Hz) corresponding to an NfmMode preset.
     */
    [[nodiscard]] float peakDeviationFor(NfmMode mode);

    /**
     * @brief Display name for an NfmMode (matches the --mode CLI value).
     */
    [[nodiscard]] const char* modeName(NfmMode mode);

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], NfmParameters& params);

    /**
     * @brief Run the NBFM transmitter command.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pinfm
