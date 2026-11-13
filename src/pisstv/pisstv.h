/**
 * @file pisstv.h
 * @brief CLI/runtime declarations for the slow-scan television (SSTV) transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cli_parse_result.h"
#include "sstv_mode.h"

namespace pisstv {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{8192};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief DMA sample rate in Hz.
     */
    inline constexpr uint32_t SAMPLE_RATE{48'000};

    /**
     * @brief Number of samples queued per librpitx::SetFrequencySamples call (~21 ms at 48 kHz).
     *
     * Small enough for prompt SIGTERM/SIGINT response, large enough to
     * amortise per-call overhead inside librpitx.
     */
    inline constexpr std::size_t DMA_BLOCK_SAMPLES{1024};

    /**
     * @brief Frequency deviation of the idle samples around the pictures: the unmodulated carrier.
     */
    inline constexpr float IDLE_DEVIATION_HZ{0.0F};

    /**
     * @brief Duration of the unmodulated carrier that follows every picture, in milliseconds.
     */
    inline constexpr double PICTURE_GAP_MS{500.0};

    /**
     * @brief SSTV parameters extracted from argv.
     */
    struct SstvParameters {
        uint64_t transmissionFrequency{0};
        const SstvMode* mode{&SSTV_MODES.front()};  ///< Default to Martin M1, the mode of the upstream binary.
        std::string imagePath{""};
        bool useStdin{false};
        bool loop{false};
    };

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], SstvParameters& params);

    /**
     * @brief Run the SSTV transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pisstv
