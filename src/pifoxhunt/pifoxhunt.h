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
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cli_parse_result.h"
#include "morse_encoder.h"

namespace pifoxhunt {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{1024};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief DMA sample rate in Hz.
     */
    inline constexpr uint32_t SAMPLE_RATE{2'000};

    /**
     * @brief Largest number of Morse code samples queued per librpitx::SetIQSamples call (16 ms at the sample rate).
     *
     * Small enough for a prompt response to a stop signal, large enough to
     * amortise per-call overhead inside librpitx.
     */
    inline constexpr std::size_t DMA_BLOCK_SAMPLES{32};

    /**
     * @brief Default harmonic passed to iqdmasync.
     */
    inline constexpr int DEFAULT_HARMONIC{1};

    /**
     * @brief Sample of iqdmasync MODE_FREQ_A that keys the carrier off.
     */
    inline constexpr std::complex<float> CARRIER_OFF_SAMPLE{0.0F, 0.0F};

    /**
     * @brief Frequency of the AF tone of the A2A mode in Hz.
     */
    inline constexpr uint32_t AF_TONE_HZ{1'000};

    /**
     * @brief Samples of half a period of the AF tone.
     */
    inline constexpr std::size_t TONE_HALF_PERIOD{SAMPLE_RATE / (2 * AF_TONE_HZ)};
    static_assert(SAMPLE_RATE % (2 * AF_TONE_HZ) == 0,
                  "every half period of the AF tone must be a whole number of samples");

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
     * @brief Operating period: a minute of the cycle for transmitters 1 - 5, every minute for the finish beacon.
     */
    inline constexpr std::chrono::minutes OPERATING_PERIOD{1};

    /**
     * @brief Time by which the clock may be read for the next code later than one code length after this one.
     *
     * A block behind carrier-off blocks, and as much again for the jitter of the reading; the identification
     * follows a code unless the next code and the identification fit with this to spare.
     */
    inline constexpr std::chrono::microseconds NEXT_CODE_TOLERANCE{2 * DMA_BLOCK_SAMPLES * 1'000'000 / SAMPLE_RATE};

    /**
     * @brief Interval between two checks of the stop flag and the system clock while a transmitter waits for its
     *        first operating period.
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
     * @brief Emission mode of the IARU ARDF rules: the pad drive of a key-down symbol in the two half periods of the
     *        AF tone, as an iqdmasync MODE_FREQ_A amplitude (0 keys the carrier off, 1 - 8 drive 2 - 16 mA).
     */
    struct EmissionMode {
        std::string_view name;  ///< --mode value.
        int firstHalfLevel;     ///< Pad drive level in the first half period of the AF tone.
        int secondHalfLevel;    ///< Pad drive level in the second half period, the first one's for no tone.
    };

    /**
     * @brief Keyed carrier (A1A) of the 3.5 MHz band at 16 mA, and keyed carrier modulated by the AF tone (A2A) of
     *        the 144 MHz band at 14 mA and 2 mA: a modulation depth of about 77 % at 144 MHz.
     */
    inline constexpr std::array<EmissionMode, 2> EMISSION_MODES{{
        {"A1A", 8, 8},
        {"A2A", 7, 1},
    }};

    /**
     * @brief Whether the AF tone modulates the carrier of mode.
     */
    [[nodiscard]] constexpr bool isToneModulated(const EmissionMode& mode) noexcept {
        return mode.firstHalfLevel != mode.secondHalfLevel;
    }

    /**
     * @brief Longest callsign: DE and the callsign fit into a minute behind the longest code even at the lowest speed.
     */
    inline constexpr std::size_t MAX_CALLSIGN_LENGTH{15};

    /**
     * @brief Fox hunting transmitter parameters extracted from argv.
     */
    struct FoxhuntParameters {
        uint64_t transmissionFrequency{0};
        const FoxTransmitter* transmitter{&FOX_TRANSMITTERS.front()};  ///< Default to the finish beacon.
        const EmissionMode* mode{&EMISSION_MODES.front()};             ///< Default to A1A.
        float wpm{MIN_WPM};                                            ///< Default to the lowest speed of the rules.
        std::string callsign;                                          ///< Empty for no identification.
    };

    /**
     * @brief Handler of the stop signals installed by installStopSignalHandlers.
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
