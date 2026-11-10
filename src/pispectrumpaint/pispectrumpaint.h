/**
 * @file pispectrumpaint.h
 * @brief CLI/runtime declarations for the spectrum painting transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cli_parse_result.h"

namespace pispectrumpaint {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{4096};

    /**
     * @brief Requested DMA channel; librpitx overrides it (channel 7 on BCM2711, 14 on the earlier SoCs).
     */
    inline constexpr int DMA_CHANNEL{14};

    /**
     * @brief Depth of the PCM FIFO that paces the DMA engine, in samples.
     *
     * librpitx requests DMA transfers while the FIFO is not full, so the
     * samples that fill it after the start are played back to back.
     */
    inline constexpr std::size_t PCM_FIFO_DEPTH{64};

    /**
     * @brief GPIO that carries the carrier (GPCLK0), the one librpitx::iqdmasync enables.
     */
    inline constexpr int CARRIER_GPIO{4};

    /**
     * @brief Default DMA sample rate in Hz - one picture pixel per sample (32 ms per 320-pixel row).
     */
    inline constexpr int DEFAULT_SAMPLE_RATE{10'000};

    /**
     * @brief Minimum accepted DMA sample rate in Hz.
     */
    inline constexpr int MIN_SAMPLE_RATE{1'000};

    /**
     * @brief Maximum accepted DMA sample rate in Hz.
     */
    inline constexpr int MAX_SAMPLE_RATE{250'000};

    /**
     * @brief Maximum painted RF bandwidth in Hz accepted on the command line.
     *
     * A sanity limit only: the carrier is modulated through the 20-bit
     * fractional divider of the PLL, so the band also has to fit between two
     * integer steps of the PLL multiplier (XOSC / divider, which shrinks with
     * the carrier frequency) - checked at run time once the divider is known.
     */
    inline constexpr int MAX_BANDWIDTH{2'000'000};

    /**
     * @brief Minimum rate of the PLL that generates the carrier, in Hz.
     *
     * The BCM2835 / BCM2711 PLLs run from 600 MHz; librpitx accepts
     * dividers down to a 200 MHz PLL rate, and such a setting freezes the board.
     */
    inline constexpr double MIN_PLL_RATE_HZ{600e6};

    /**
     * @brief Default harmonic passed to iqdmasync.
     */
    inline constexpr int DEFAULT_HARMONIC{1};

    /**
     * @brief Spectrum painting parameters extracted from argv.
     */
    struct SpectrumPaintParameters {
        uint64_t transmissionFrequency{0};
        float bandwidth{0.0F};
        int sampleRate{DEFAULT_SAMPLE_RATE};
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
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], SpectrumPaintParameters& params);

    /**
     * @brief Run the spectrum painting transmitter.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pispectrumpaint
