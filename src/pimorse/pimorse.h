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
#include <vector>

#include "cli_parse_result.h"

namespace pimorse {
    /**
     * @brief OOK upsample factor: DMA samples per symbol (symbol duration = 1 / symbolRate seconds).
     *
     * Every character is a DMA chain of its own, which starts only once 65 samples have filled the
     * pacing FIFO; 1250 samples per symbol keep that wait at about 5 % of a dit.
     */
    inline constexpr float OOK_UPSAMPLE{1250.0F};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

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
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

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
