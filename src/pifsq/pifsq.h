/**
 * @file pifsq.h
 * @brief CLI/runtime declarations for the FSQ (Fast Simple QSO) transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
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
#include "fsq_encoder.h"

namespace pifsq {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{2048};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief DMA sample rate in Hz: the sample rate of the FSQ modems of the FSQCall program and fldigi, in
     *        whose samples the lengths of the symbols are counted.
     */
    inline constexpr uint32_t SAMPLE_RATE{12'000};

    /**
     * @brief Number of samples queued per librpitx::SetFrequencySamples call (~21 ms at the sample rate).
     *
     * Small enough for a prompt response to a stop signal, large enough to
     * amortise per-call overhead inside librpitx.
     */
    inline constexpr std::size_t DMA_BLOCK_SAMPLES{256};

    /**
     * @brief Frequency deviation of the idle samples around the transmission: the unmodulated carrier, which
     *        lies outside the band a receiver looks for the tones in.
     */
    inline constexpr float IDLE_DEVIATION_HZ{0.0F};

    /**
     * @brief Frequency of the lowest tone above the carrier in Hz, where the FSQCall program sends it.
     */
    inline constexpr float BASE_TONE_HZ{1'350.0F};

    /**
     * @brief Spacing of the tones in Hz: three bins of the 4096-sample transform of the FSQ modems.
     */
    inline constexpr float TONE_SPACING_HZ{8.7890625F};

    /**
     * @brief Frequency of the highest tone above the carrier in Hz.
     */
    inline constexpr float HIGHEST_TONE_HZ{BASE_TONE_HZ + static_cast<float>(TONE_COUNT - 1) * TONE_SPACING_HZ};

    /**
     * @brief FSQ mode: the length of a symbol.
     */
    struct FsqMode {
        std::string_view name;      ///< --mode value, the nominal baud rate.
        std::size_t symbolSamples;  ///< Length of a symbol in samples.
    };

    /**
     * @brief FSQ modes 2, 3, 4.5 and 6 with the symbol lengths the FSQCall program and fldigi send: 512, 341,
     *        256 and 171 ms.
     */
    inline constexpr std::array<FsqMode, 4> FSQ_MODES{{
        {"2", 6'144},
        {"3", 4'096},
        {"4.5", 3'072},
        {"6", 2'048},
    }};

    static_assert(std::ranges::all_of(FSQ_MODES,
                                      [](const FsqMode& mode) { return mode.symbolSamples % DMA_BLOCK_SAMPLES == 0; }),
                  "every symbol must be a whole number of DMA blocks");

    /**
     * @brief --type values, in the order of MessageType.
     */
    inline constexpr std::array<std::string_view, 2> TYPE_NAMES{"directed", "undirected"};

    /**
     * @brief Display name for a MessageType (matches the --type CLI value).
     */
    [[nodiscard]] constexpr std::string_view typeName(MessageType type) noexcept {
        return TYPE_NAMES[static_cast<std::size_t>(type)];
    }

    /**
     * @brief Callsign that directs a message to all stations.
     */
    inline constexpr std::string_view ALL_STATIONS{"allcall"};

    /**
     * @brief FSQ parameters extracted from argv.
     */
    struct FsqParameters {
        uint64_t transmissionFrequency{0};
        const FsqMode* mode{&FSQ_MODES.front()};  ///< Default to FSQ 2, the speed of the upstream binary.
        MessageType type{MessageType::Directed};  ///< Default to a directed message, as the upstream binary.
        std::string callsign;
        std::string destination{ALL_STATIONS};  ///< Default to all stations.
        std::string message;
    };

    /**
     * @brief Stop signal handler, installed by installStopSignalHandlers.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FsqParameters& params);

    /**
     * @brief Run the FSQ transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pifsq
