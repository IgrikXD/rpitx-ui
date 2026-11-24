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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cli_parse_result.h"

namespace pimorse {
    /**
     * @brief DMA samples per symbol (symbol duration = 1 / symbolRate seconds).
     *
     * Few enough that up to MAX_WPM the gaps behind the characters stay within 0.05 dits on BCM2711, and enough
     * that MIN_WPM keeps the pacing rate within the range of the PCM clock divider.
     */
    inline constexpr std::size_t OOK_UPSAMPLE{500};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief Divisor to convert words-per-minute to OOK symbol rate.
     */
    inline constexpr float WPM_TO_SYMBOL_RATE_DIVISOR{1.2F};

    /**
     * @brief Minimum accepted speed in words per minute.
     */
    inline constexpr float MIN_WPM{1.0F};

    /**
     * @brief Maximum accepted speed in words per minute.
     */
    inline constexpr float MAX_WPM{200.0F};

    /**
     * @brief Morse transmitter parameters extracted from argv.
     */
    struct PimorseParameters {
        uint64_t transmissionFrequency{0};
        float wpm{0.0F};
        std::string message;
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PimorseParameters& params);

    /**
     * @brief Convert a text message into the CW OOK binary stream of each supported character.
     */
    [[nodiscard]] std::vector<std::string> encodeMessage(std::string_view message);

    /**
     * @brief Transmit the CW OOK binary streams of the characters at the given frequency and symbol rate,
     *        one character after the other, until all are sent or a stop is requested.
     */
    void sendCwOok(uint64_t transmissionFrequency, float symbolRate, const std::vector<std::string>& characters);

    /**
     * @brief Run the Morse code CW OOK transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pimorse
