/**
 * @file pissb.h
 * @brief CLI/runtime declarations for the streaming SSB modulator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <limits>

#include "ssb_processor.h"

namespace pissb {
    /**
     * @brief Block size for PCM sample processing (~21 ms at 48 kHz).
     */
    inline constexpr int BLOCK_SIZE{1024};

    /**
     * @brief Normalization divisor for int16_t -> float [-1.0, 1.0] conversion (2^15).
     */
    inline constexpr float PCM16_MAX{static_cast<float>(std::numeric_limits<int16_t>::max()) + 1.0F};

    /**
     * @brief SSB parameters extracted from argv.
     */
    struct PissbParameters {
        SsbMode mode{SsbMode::USB};
    };

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse command-line arguments.
     */
    void parseArgs(int argc, char* argv[], PissbParameters& params);

    /**
     * @brief Run the streaming SSB modulator.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pissb
