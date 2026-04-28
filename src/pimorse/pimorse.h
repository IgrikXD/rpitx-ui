/**
 * @file pimorse.h
 * @brief CLI/runtime declarations for the Morse code CW OOK transmitter.
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
#include <string_view>

#include "cli_parse_result.h"

namespace pimorse {
    /**
     * @brief OOK upsample factor (symbol duration = upsample / symbolrate seconds).
     */
    inline constexpr float OOK_UPSAMPLE{125.0F};

    /**
     * @brief DMA bit depth for ookburst.
     */
    inline constexpr int OOK_DMA_BITS{14};

    /**
     * @brief Divisor to convert words-per-minute to OOK symbol rate.
     */
    inline constexpr float WPM_TO_SYMBOL_RATE_DIVISOR{1.2F};

    /**
     * @brief Morse transmitter parameters extracted from argv.
     */
    struct PimorseParameters {
        uint64_t transmissionFrequency{0};
        float wpm{0.0F};
        std::string message;
    };

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PimorseParameters& params);

    /**
     * @brief Convert a text message into a contiguous CW OOK binary stream.
     */
    [[nodiscard]] std::string encodeMessage(std::string_view message);

    /**
     * @brief Transmit a CW OOK binary string at the given frequency and symbol rate.
     */
    void sendCwOok(float transmissionFrequency, float symbolRate, std::string_view cw);

    /**
     * @brief Run the Morse code CW OOK transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pimorse
