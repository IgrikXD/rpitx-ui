/**
 * @file pifmrds.h
 * @brief CLI/runtime declarations for the wide-band FM with RDS transmitter.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cli_parse_result.h"

namespace pifmrds {
    /**
     * @brief DMA sample buffer depth.
     */
    inline constexpr uint32_t DMA_FIFO_SIZE{16384};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    inline constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief MPX / DMA sample rate in Hz.
     */
    inline constexpr int MPX_SAMPLE_RATE{228'000};

    /**
     * @brief Target MPX output frames per processing block (~18 ms at 228 kHz).
     */
    inline constexpr int TARGET_OUTPUT_FRAMES{4096};

    /**
     * @brief Polyphase resampler taps per phase.
     */
    inline constexpr int RESAMPLER_TAPS_PER_PHASE{32};

    /**
     * @brief Polyphase resampler LPF cutoff in Hz.
     */
    inline constexpr float RESAMPLER_LPF_CUTOFF{15'000.0F};

    /**
     * @brief Peak FM deviation in Hz.
     */
    inline constexpr float PEAK_DEVIATION{75'000.0F};

    /**
     * @brief Minimum accepted input sample rate in Hz.
     */
    inline constexpr int MIN_INPUT_RATE{8'000};

    /**
     * @brief Maximum accepted input sample rate in Hz.
     */
    inline constexpr int MAX_INPUT_RATE{192'000};

    /**
     * @brief Default RDS Programme Identification code.
     */
    inline constexpr uint16_t DEFAULT_RDS_PI{0x1234};

    /**
     * @brief Default RDS Programme Service (PS) name.
     */
    inline constexpr std::string_view DEFAULT_RDS_PS{"rpitx-ui"};

    /**
     * @brief Default RDS RadioText (RT).
     */
    inline constexpr std::string_view DEFAULT_RDS_RT{"rpitx-ui Broadcast WFM with RDS"};

    /**
     * @brief Pre-emphasis time-constant preset.
     */
    enum class PreEmphasisMode : uint8_t {
        Eu50,
        Us75,
    };

    /**
     * @brief FM-RDS parameters extracted from argv.
     */
    struct FmRdsParameters {
        uint64_t transmissionFrequency{0};
        std::string audioPath{""};
        bool loop{false};
        uint16_t pi{DEFAULT_RDS_PI};
        std::string ps{DEFAULT_RDS_PS};
        std::string rt{DEFAULT_RDS_RT};
        PreEmphasisMode preEmph{PreEmphasisMode::Eu50};
    };

    /**
     * @brief Map a pre-emphasis preset to its time constant in seconds.
     */
    [[nodiscard]] float preEmphasisTauFor(PreEmphasisMode mode);

    /**
     * @brief Display name for a PreEmphasisMode (matches the --pre-emphasis CLI value).
     */
    [[nodiscard]] const char* preEmphasisName(PreEmphasisMode mode);

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     * @param sig Signal number.
     */
    void handleSignal(int sig);

    /**
     * @brief Parse and validate command-line arguments via CLI11.
     */
    [[nodiscard]] rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FmRdsParameters& params);

    /**
     * @brief Run the FM-RDS transmitter command.
     */
    [[nodiscard]] int run(int argc, char* argv[]);
}  // namespace pifmrds
