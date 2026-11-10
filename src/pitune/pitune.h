/**
 * @file pitune.h
 * @brief CLI/runtime declarations for the unmodulated carrier transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "cli_parse_result.h"

namespace pitune {
    /**
     * @brief GPIO that carries the carrier (GPCLK0).
     */
    inline constexpr int CARRIER_GPIO{4};

    /**
     * @brief Guard band for the PLL divider choice of librpitx, in Hz: the carrier is not modulated.
     */
    inline constexpr int PLL_GUARD_BAND_HZ{0};

    /**
     * @brief Minimum rate of the PLL that generates the carrier, in Hz.
     *
     * The BCM2835 / BCM2711 PLLs run from 600 MHz; librpitx accepts
     * dividers down to a 200 MHz PLL rate, and such a setting freezes the board.
     */
    inline constexpr double MIN_PLL_RATE_HZ{600e6};

    /**
     * @brief Largest clock correction accepted by --ppm, in ppm either way (the limit of the kernel NTP correction).
     */
    inline constexpr int MAX_PPM{500};

    /**
     * @brief Carrier parameters extracted from argv.
     */
    struct TuneParameters {
        uint64_t transmissionFrequency{0};
        std::optional<double> ppm;  ///< Engaged iff --ppm was given on the CLI.
    };

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], TuneParameters& params);

    /**
     * @brief Run the unmodulated carrier transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pitune
