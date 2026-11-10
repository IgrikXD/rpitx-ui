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

#include <chrono>
#include <cstdint>

#include "cli_parse_result.h"

namespace pitune {
    /**
     * @brief GPIO that carries the carrier (GPCLK0).
     */
    inline constexpr int CARRIER_GPIO{4};

    /**
     * @brief Half the bandwidth around the carrier for the PLL setup, in Hz: the carrier is not modulated.
     */
    inline constexpr double CARRIER_HALF_BANDWIDTH_HZ{0.0};

    /**
     * @brief Interval between two checks of the stop flag while the carrier is on.
     */
    inline constexpr std::chrono::milliseconds STOP_CHECK_INTERVAL{100};

    /**
     * @brief Carrier parameters extracted from argv.
     */
    struct TuneParameters {
        uint64_t transmissionFrequency{0};
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], TuneParameters& params);

    /**
     * @brief Run the unmodulated carrier transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pitune
