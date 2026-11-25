/**
 * @file pifoxhunt.h
 * @brief CLI/runtime declarations for the amateur radio direction finding (fox hunting) transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cli_parse_result.h"
#include "morse_encoder.h"

namespace pifoxhunt {
    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief Rate in Hz at which ookburst keys the carrier on or off, one sample at a time.
     */
    inline constexpr uint32_t SAMPLE_RATE{2'000};

    /**
     * @brief Frequency of the AF tone of the A2A mode in Hz.
     */
    inline constexpr uint32_t AF_TONE_HZ{1'000};

    /**
     * @brief Minimum keying speed in words per minute, the lowest one of the IARU ARDF rules.
     */
    inline constexpr float MIN_WPM{8.0F};

    /**
     * @brief Maximum keying speed in words per minute, the highest one of the IARU ARDF rules.
     */
    inline constexpr float MAX_WPM{15.0F};

    /**
     * @brief Number of samples of one Morse symbol (a dit) at wpm words per minute, rounded to a whole sample.
     */
    [[nodiscard]] constexpr std::size_t samplesPerSymbol(float wpm) noexcept {
        return static_cast<std::size_t>(static_cast<float>(SAMPLE_RATE) * WPM_TO_SYMBOL_RATE_DIVISOR / wpm + 0.5F);
    }

    /**
     * @brief Cycle of the system clock in which transmitters 1 - 5 operate one after the other.
     */
    inline constexpr std::chrono::minutes CYCLE{5};

    /**
     * @brief Operating period of each of transmitters 1 - 5 in the cycle.
     */
    inline constexpr std::chrono::minutes OPERATING_PERIOD{1};

    /**
     * @brief Interval between two checks of the stop flag while a transmitter waits for its operating period.
     */
    inline constexpr std::chrono::milliseconds STOP_CHECK_INTERVAL{100};

    /**
     * @brief Transmitter of the classic competition: the code it sends and its minute of the cycle.
     */
    struct FoxTransmitter {
        std::string_view code;  ///< --code value, sent in Morse code.
        int minute;             ///< Minute of the cycle the transmitter operates in (1 - 5), 0 for continuously.
    };

    /**
     * @brief Transmitters of the classic competition of the IARU ARDF rules: the finish beacon and transmitters 1 - 5.
     */
    inline constexpr std::array<FoxTransmitter, 6> FOX_TRANSMITTERS{{
        {"MO", 0},
        {"MOE", 1},
        {"MOI", 2},
        {"MOS", 3},
        {"MOH", 4},
        {"MO5", 5},
    }};

    /**
     * @brief Emission mode of the IARU ARDF rules: how a key-down symbol keys the carrier.
     */
    struct EmissionMode {
        std::string_view name;       ///< --mode value.
        std::size_t toneHalfPeriod;  ///< Samples of half a period of the AF tone switching the carrier, 0 for none.
    };

    /**
     * @brief Keyed carrier (A1A) of the 3.5 MHz band and keyed carrier switched on and off by the AF tone (A2A) of
     *        the 144 MHz band.
     */
    inline constexpr std::array<EmissionMode, 2> EMISSION_MODES{{
        {"A1A", 0},
        {"A2A", SAMPLE_RATE / (2 * AF_TONE_HZ)},
    }};
    static_assert(SAMPLE_RATE % (2 * AF_TONE_HZ) == 0,
                  "every half period of the AF tone must be a whole number of samples");

    /**
     * @brief Fox hunting transmitter parameters extracted from argv.
     */
    struct FoxhuntParameters {
        uint64_t transmissionFrequency{0};
        const FoxTransmitter* transmitter{&FOX_TRANSMITTERS.front()};  ///< Default to the finish beacon.
        const EmissionMode* mode{&EMISSION_MODES.front()};             ///< Default to A1A.
        float wpm{MIN_WPM};                                            ///< Default to the lowest speed of the rules.
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FoxhuntParameters& params);

    /**
     * @brief Run the fox hunting transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pifoxhunt
