/**
 * @file piook.h
 * @brief CLI/runtime declarations for the on-off keying (OOK) transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
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
#include <vector>

#include "cli_parse_result.h"
#include "ook_sequence.h"

namespace piook {
    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief DMA sample rate in Hz: the carrier is keyed on a grid of 10 us, that of the upstream binary.
     */
    inline constexpr uint32_t SAMPLE_RATE{100'000};

    /**
     * @brief Duration of a sample in microseconds: the step of the grid and the shortest pulse or gap.
     */
    inline constexpr uint32_t SAMPLE_TIME_US{1'000'000 / SAMPLE_RATE};
    static_assert(SAMPLE_TIME_US * SAMPLE_RATE == 1'000'000, "a sample must last a whole number of microseconds");

    /**
     * @brief Largest number of samples of one run of the DMA chain.
     *
     * The DMA channels librpitx uses count the bytes of a transfer in 16 bits, and the pacing of a sample
     * takes a word of 4 bytes.
     */
    inline constexpr uint32_t MAX_RUN_SAMPLES{0xFFFF / sizeof(uint32_t)};

    /**
     * @brief Largest number of runs of the DMA chain, which takes 68 bytes of GPU memory per run (1.1 MB).
     */
    inline constexpr std::size_t MAX_CHAIN_RUNS{16'384};

    /**
     * @brief Interval between two checks of the stop flag while the DMA engine sends the chain.
     */
    inline constexpr std::chrono::milliseconds STOP_CHECK_INTERVAL{1};

    /**
     * @brief --type values, in the order of MessageType.
     */
    inline constexpr std::array<std::string_view, 2> TYPE_NAMES{"symbols", "timings"};

    /**
     * @brief Display name for a MessageType (matches the --type CLI value).
     */
    [[nodiscard]] constexpr std::string_view typeName(MessageType type) noexcept {
        return TYPE_NAMES[static_cast<std::size_t>(type)];
    }

    /**
     * @brief OOK parameters extracted from argv.
     */
    struct OokParameters {
        uint64_t transmissionFrequency{0};
        MessageType type{MessageType::Symbols};  ///< Default to a message of 1 and 0, as the upstream binary.
        std::vector<KeyingInterval> sequence;    ///< Pulses and gaps of --message.
        double symbolTime{0.0};                  ///< Microseconds; only with MessageType::Symbols.
        uint32_t repeat{1};                      ///< Default to a single transmission of the sequence.
        double pause{0.0};                       ///< Microseconds between the repetitions; none by default.
    };

    /**
     * @brief Stop signal handler, installed by installStopSignalHandlers.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], OokParameters& params);

    /**
     * @brief Run the OOK transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace piook
