/**
 * @file pipocsag.h
 * @brief CLI/runtime declarations for the POCSAG pager transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 16.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cli_parse_result.h"
#include "pocsag_encoder.h"

namespace pipocsag {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{8192};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief DMA sample rate in Hz: the lowest one that holds a whole number of samples per bit in every mode.
     */
    inline constexpr uint32_t SAMPLE_RATE{38'400};

    /**
     * @brief Bit rates of the POCSAG modes in bit/s: POCSAG 512, 1200 and 2400; the first one is the default.
     */
    inline constexpr std::array<uint32_t, 3> BIT_RATES{512, 1'200, 2'400};
    static_assert(std::ranges::all_of(BIT_RATES, [](uint32_t bitRate) { return SAMPLE_RATE % bitRate == 0; }),
                  "every bit rate must divide the sample rate");

    /**
     * @brief Frequency deviation of the two tones from the carrier in Hz: a 0 is sent above the carrier and a 1
     *        below it, the other way round with --invert.
     */
    inline constexpr float DEVIATION_HZ{4'500.0F};

    /**
     * @brief POCSAG parameters extracted from argv.
     */
    struct PocsagParameters {
        uint64_t transmissionFrequency{0};
        uint32_t bitRate{BIT_RATES.front()};  ///< Default to POCSAG 512, the original bit rate of the code.
        uint32_t address{0};
        MessageType type{MessageType::Alphanumeric};  ///< Default to an alphanumeric message, as the upstream binary.
        Function function{Function::D};
        std::string message;
        bool invert{false};  ///< Swap the two tones: a 1 above the carrier, a 0 below it.
    };

    /**
     * @brief Function a call gets without --function: D for an alphanumeric message, A otherwise.
     *
     * The Recommendation gives numeric-only messages function A and alphanumeric ones function D.
     */
    [[nodiscard]] constexpr Function defaultFunction(MessageType type) noexcept {
        return type == MessageType::Alphanumeric ? Function::D : Function::A;
    }

    /**
     * @brief --type values, in the order of MessageType.
     */
    inline constexpr std::array<std::string_view, 3> TYPE_NAMES{"alphanumeric", "numeric", "tone-only"};

    /**
     * @brief --function values, in the order of Function.
     */
    inline constexpr std::array<std::string_view, 4> FUNCTION_NAMES{"a", "b", "c", "d"};

    /**
     * @brief Display name for a MessageType (matches the --type CLI value).
     */
    [[nodiscard]] constexpr std::string_view typeName(MessageType type) noexcept {
        return TYPE_NAMES[static_cast<std::size_t>(type)];
    }

    /**
     * @brief Display name for a Function (matches the --function CLI value).
     */
    [[nodiscard]] constexpr std::string_view functionName(Function function) noexcept {
        return FUNCTION_NAMES[static_cast<std::size_t>(function)];
    }

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PocsagParameters& params);

    /**
     * @brief Run the POCSAG transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pipocsag
