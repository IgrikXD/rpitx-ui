/**
 * @file piopera.h
 * @brief CLI/runtime declarations for the Opera beacon transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 18.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cli_parse_result.h"

namespace piopera {
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
    inline constexpr uint32_t SAMPLE_RATE{1'000};

    /**
     * @brief Number of samples queued per librpitx::SetIQSamples call (16 ms at the sample rate).
     *
     * Small enough for a prompt response to a stop signal, large enough to
     * amortise per-call overhead inside librpitx.
     */
    inline constexpr std::size_t DMA_BLOCK_SAMPLES{16};

    /**
     * @brief Default harmonic passed to iqdmasync.
     */
    inline constexpr int DEFAULT_HARMONIC{1};

    /**
     * @brief Sample of iqdmasync MODE_FREQ_A that keys the carrier on: no frequency offset, the strongest pad drive.
     */
    inline constexpr std::complex<float> CARRIER_ON_SAMPLE{0.0F, 8.0F};

    /**
     * @brief Sample of iqdmasync MODE_FREQ_A that keys the carrier off.
     */
    inline constexpr std::complex<float> CARRIER_OFF_SAMPLE{0.0F, 0.0F};

    /**
     * @brief Opera mode: the duration of a symbol.
     */
    struct OperaMode {
        std::string_view name;  ///< --mode value, named after the transmission time in minutes.
        uint32_t symbolMs;      ///< Duration of a symbol in ms, 256 ms times the number of the mode.
    };

    /**
     * @brief Opera modes of the amateur bands, from Op05 for 2 m - 23 cm to Op16 and Op32 for 2200 m.
     */
    inline constexpr std::array<OperaMode, 7> OPERA_MODES{{
        {"op05", 128},
        {"op1", 256},
        {"op2", 512},
        {"op4", 1'024},
        {"op8", 2'048},
        {"op16", 4'096},
        {"op32", 8'192},
    }};

    /**
     * @brief Number of samples of one symbol of mode.
     */
    [[nodiscard]] constexpr std::size_t samplesPerSymbol(const OperaMode& mode) noexcept {
        return static_cast<std::size_t>(mode.symbolMs) * SAMPLE_RATE / 1'000;
    }

    static_assert(std::ranges::all_of(OPERA_MODES,
                                      [](const OperaMode& mode) {
                                          return mode.symbolMs * SAMPLE_RATE % 1'000 == 0 &&
                                                 samplesPerSymbol(mode) % DMA_BLOCK_SAMPLES == 0;
                                      }),
                  "every symbol must be a whole number of DMA blocks");

    /**
     * @brief Opera parameters extracted from argv.
     */
    struct OperaParameters {
        uint64_t transmissionFrequency{0};
        const OperaMode* mode{&OPERA_MODES.front()};  ///< Default to Op05, the mode of the upstream UI.
        std::string callsign;
    };

    /**
     * @brief Handler of the stop signals installed by installStopSignalHandlers.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], OperaParameters& params);

    /**
     * @brief Run the Opera beacon transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace piopera
