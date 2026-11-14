/**
 * @file pirtty.h
 * @brief CLI/runtime declarations for the radioteletype (RTTY) transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cli_parse_result.h"

namespace pirtty {
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
     * @brief Frequency of the space tone above the carrier in Hz: the start bit and the 0 bits.
     */
    inline constexpr float SPACE_TONE_HZ{1'000.0F};

    /**
     * @brief Frequency shift of the RTTY modes in Hz: the mark tone lies this far above the space tone.
     */
    inline constexpr float FREQUENCY_SHIFT_HZ{170.0F};

    /**
     * @brief Frequency of the mark tone above the carrier in Hz: the 1 bits, the stop bits and the idle line.
     */
    inline constexpr float MARK_TONE_HZ{SPACE_TONE_HZ + FREQUENCY_SHIFT_HZ};

    /**
     * @brief Length of the stop bit in bits.
     */
    inline constexpr double STOP_BITS{1.5};

    /**
     * @brief RTTY mode: the signalling rate of the frequency shift keying.
     */
    struct RttyMode {
        std::string_view name;  ///< --mode value.
        double baudRate;        ///< Bits per second.
    };

    /**
     * @brief RTTY modes of the amateur bands, as fldigi sends RTTY-45, RTTY-50 and RTTY-75N.
     */
    inline constexpr std::array<RttyMode, 3> RTTY_MODES{{
        {"45", 45.45},
        {"50", 50.0},
        {"75", 75.0},
    }};

    /**
     * @brief Number of samples of one bit of mode, rounded to a whole sample (22 ms at 45.45 baud).
     */
    [[nodiscard]] constexpr std::size_t samplesPerBit(const RttyMode& mode) noexcept {
        return static_cast<std::size_t>(SAMPLE_RATE / mode.baudRate + 0.5);
    }

    /**
     * @brief RTTY parameters extracted from argv.
     */
    struct RttyParameters {
        uint64_t transmissionFrequency{0};
        const RttyMode* mode{&RTTY_MODES.front()};  ///< Default to RTTY 45, the mode of the upstream binary.
        std::string message;
    };

    /**
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], RttyParameters& params);

    /**
     * @brief Run the RTTY transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pirtty
