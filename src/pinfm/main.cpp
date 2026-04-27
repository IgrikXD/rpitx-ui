/**
 * @file main.cpp
 * @brief Narrow-band FM (NBFM) transmitter.
 *
 * Reads audio from a file (any format libsndfile supports), downmixes to
 * mono, resamples to 48 kHz if needed, produces a per-sample frequency-
 * deviation stream (+-2.5 kHz narrow or +-5 kHz wide peak), and drives
 * librpitx::ngfmdmasync directly at the requested carrier frequency.
 * Replaces the legacy csdr-based testnfm.sh pipeline, which routed audio
 * through an IQ pipe and gave no bandwidth containment or level control.
 * Transmission runs until the audio ends (or forever when -l is set), or
 * until SIGTERM / SIGINT (the rpitx-ui launcher stops the process centrally
 * via killall when the user dismisses the dialog).
 *
 * @note Usage: pinfm <freq_Hz> -a <file> [-l] [-m <mode>] [-h]
 *   - -a   Path to the audio file (any format libsndfile understands).
 *   - -l   Loop the audio file (replay from the start on EOF).
 *   - -m   NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)
 *   - -h   Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <librpitx/librpitx.h>

#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio_pipeline.h"
#include "cli_utils.h"
#include "libsndfile_audio_source.h"
#include "nfm_processor.h"

namespace {
    /**
     * @brief DMA sample buffer depth.
     */
    constexpr uint32_t DMA_FIFO_SIZE{4096};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief Internal NBFM processing rate in Hz (also the DMA rate).
     *
     * 48 kHz is the rate the NFM processor's HPF / LPF / AGC are designed
     * around and the rate at which ngfmdmasync consumes deviation samples.
     * Source rate matching is handled by the polyphase resampler stage.
     */
    constexpr uint32_t TARGET_SAMPLE_RATE{48'000};

    /**
     * @brief Target output frames per processing block (~21 ms at 48 kHz).
     */
    constexpr int TARGET_OUTPUT_FRAMES{1024};

    /**
     * @brief Polyphase resampler taps per phase.
     */
    constexpr int RESAMPLER_TAPS_PER_PHASE{32};

    /**
     * @brief Polyphase resampler LPF cutoff in Hz.
     *
     * Matches the NBFM voice-bandwidth guard in NfmProcessor (3000 Hz). The
     * runtime min(LPF_CUTOFF, 0.45 * min(srcRate, target)) cap ensures the
     * cutoff stays safely below Nyquist for both source and target rates.
     */
    constexpr float RESAMPLER_LPF_CUTOFF{3'000.0F};

    /**
     * @brief Minimum accepted input sample rate in Hz.
     */
    constexpr int MIN_INPUT_RATE{8'000};

    /**
     * @brief Maximum accepted input sample rate in Hz.
     */
    constexpr int MAX_INPUT_RATE{192'000};

    /**
     * @brief NBFM deviation preset mode.
     *
     * Closed-set of the two de facto NBFM deviation standards:
     *   - Narrow: +-2.5 kHz, paired with 12.5 kHz channel spacing (PMR446 /
     *     narrow-PMR / DMR / public-safety).
     *   - Wide:   +-5.0 kHz, paired with 25 kHz channel spacing (amateur
     *     2 m / 70 cm VHF/UHF FM voice).
     *
     * Encoded as a closed enum rather than a free-form Hz argument so that
     * the CLI cannot accidentally request a deviation/filter combination
     * that breaks the channel mask (the LPF is designed against these two
     * operating points).
     */
    enum class NfmMode : uint8_t {
        Narrow,
        Wide,
    };

    /**
     * @brief NfmMode textual names for CLI parsing and display.
     */
    constexpr std::array MODE_TABLE{
        NamedEnum<NfmMode>{"narrow", NfmMode::Narrow},
        NamedEnum<NfmMode>{"wide", NfmMode::Wide},
    };

    /**
     * @brief Peak deviation (Hz) corresponding to an NfmMode preset.
     *
     * Centralised so main() stays a thin wrapper and the mapping is trivial
     * to audit against the channel-mask math encoded in nfm_processor.h.
     *
     * @param mode Selected deviation preset.
     * @return Peak deviation in Hz.
     */
    [[nodiscard]] constexpr float peakDeviationFor(NfmMode mode) {
        switch (mode) {
            case NfmMode::Narrow:
                return 2'500.0F;
            case NfmMode::Wide:
                return 5'000.0F;
        }
        // Closed enum + -Wswitch guarantees this is unreachable; return 0 only
        // to silence "control reaches end of non-void function" on old toolchains.
        return 0.0F;
    }

    /**
     * @brief Atomic flag for graceful shutdown on signal reception.
     */
    std::atomic<bool> running{true};
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "std::atomic<bool> must be lock-free for signal-handler access");

    /**
     * @brief Signal handler for SIGTERM, SIGINT, and SIGPIPE.
     * @param sig Signal number.
     */
    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    /**
     * @brief Print the command-line usage to stderr.
     */
    void printUsage() {
        std::cerr << "Usage: pinfm <freq_Hz> -a <file> [options]" << std::endl
                  << "  -a <file>  Path to the audio file (libsndfile-supported format)" << std::endl
                  << "  -l         Loop the audio file (replay on EOF)" << std::endl
                  << "  -m <mode>  NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)" << std::endl
                  << "  -h         Print this help message" << std::endl
                  << "  Audio is downmixed to mono and resampled to " << TARGET_SAMPLE_RATE
                  << " Hz internally." << std::endl;
    }

    /**
     * @brief NFM parameters extracted from argv.
     */
    struct NfmParameters {
        uint64_t freq{0};
        std::string audioPath;
        bool loop{false};
        NfmMode mode{NfmMode::Wide};  ///< Default to wide (+-5 kHz, amateur VHF/UHF).
    };

    /**
     * @brief Parse and validate the single positional argument (frequency).
     */
    [[nodiscard]] ParseResult parsePositionalArgs(std::string_view freqArg, NfmParameters& params) {
        const auto freqOpt{parseNumericArg<double>(freqArg)};
        if (freqOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid frequency argument!" << std::endl;
            return ParseResult::Error;
        }
        const double freqValue{freqOpt.value()};
        if (freqValue <= 0.0 || std::isfinite(freqValue) == false || freqValue >= std::ldexp(1.0, 64)) {
            std::cerr << "[ERROR] Frequency is out of representable range!" << std::endl;
            return ParseResult::Error;
        }
        params.freq = static_cast<uint64_t>(freqValue);
        return ParseResult::Ok;
    }

    /**
     * @brief Walk the optional flag tail and populate params.
     */
    [[nodiscard]] ParseResult parseOptionalFlags(std::span<char* const> args, NfmParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            if (arg == "-l") {
                params.loop = true;
                continue;
            }
            if (arg != "-a" && arg != "-m") {
                std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
                return ParseResult::Error;
            }
            if (++i >= args.size()) {
                std::cerr << "[ERROR] Option " << arg << " requires an argument!" << std::endl;
                return ParseResult::Error;
            }
            const std::string_view value{args[i]};

            if (arg == "-a") {
                params.audioPath.assign(value);
            } else if (arg == "-m") {
                const auto mode{parseNamedEnum(value, MODE_TABLE)};
                if (mode == std::nullopt) {
                    std::cerr << "[ERROR] Unknown mode '" << value << "'. Expected narrow | wide." << std::endl;
                    return ParseResult::Error;
                }
                params.mode = mode.value();
            }
        }
        return ParseResult::Ok;
    }

    /**
     * @brief Parse and validate command-line arguments.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], NfmParameters& params) {
        if (containsFlag({argv + 1, argv + argc}, "-h")) {
            printUsage();
            return ParseResult::Help;
        }
        if (argc < 2) {
            printUsage();
            return ParseResult::Error;
        }
        if (const auto result{parsePositionalArgs(argv[1], params)}; result != ParseResult::Ok) {
            return result;
        }
        const std::span<char* const> flagArgs{argv + 2, argv + argc};
        if (const auto result{parseOptionalFlags(flagArgs, params)}; result != ParseResult::Ok) {
            return result;
        }
        if (params.audioPath.empty()) {
            std::cerr << "[ERROR] Missing required option: -a <file>!" << std::endl;
            return ParseResult::Error;
        }
        return ParseResult::Ok;
    }

}  // namespace

int main(int argc, char* argv[]) {
    NfmParameters params;
    switch (parseArgs(argc, argv, params)) {
        case ParseResult::Ok:
            break;
        case ParseResult::Help:
            return 0;
        case ParseResult::Error:
            return 1;
    }

    std::signal(SIGTERM, handleSignal);
    std::signal(SIGINT, handleSignal);
    // pinfm writes startup/shutdown status to stdout. If stdout is attached
    // to a pipe whose reader exits (e.g. `pinfm ... | tee log` where tee is
    // killed), those writes would raise SIGPIPE and abort us. Handle it as
    // a graceful shutdown instead.
    std::signal(SIGPIPE, handleSignal);

    auto source{makeFileAudioSource(params.audioPath)};
    if (source == nullptr) {
        return 1;
    }
    if (validateLoopSupport(*source, params.loop) == false) {
        return 1;
    }

    const auto fmt{source->format()};
    if (validateMonoStereoAudioFormat(fmt, MIN_INPUT_RATE, MAX_INPUT_RATE) == false) {
        return 1;
    }

    const float peakDeviation{peakDeviationFor(params.mode)};

    AudioPipeline audio{*source,
                        {
                            .loop               = params.loop,
                            .targetSampleRate   = static_cast<int>(TARGET_SAMPLE_RATE),
                            .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                            .tapsPerPhase       = RESAMPLER_TAPS_PER_PHASE,
                            .maxCutoffHz        = RESAMPLER_LPF_CUTOFF,
                            .channelMode        = AudioChannelMode::Mono,
                        }};

    std::cout << "pinfm: center=" << params.freq << " Hz, src_rate=" << fmt.sampleRate
              << " Hz, dma_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
              << ", format=" << source->description() << ", mode=" << formatNamedEnum(params.mode, MODE_TABLE)
              << " (+-" << peakDeviation << " Hz), loop=" << (params.loop ? "yes" : "no") << std::endl;

    NfmProcessor nfm{static_cast<float>(TARGET_SAMPLE_RATE), peakDeviation};
    ngfmdmasync dma{params.freq, TARGET_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

    // AudioPipeline owns source-rate input buffers, downmixing, loop-aware
    // EOF handling, and source -> 48 kHz rate conversion. outputBuf is reused
    // for NBFM deviation samples in place before handing the block to DMA.
    std::vector<float> outputBuf(audio.outputSamplesPerBlock());

    while (running.load(std::memory_order_relaxed)) {
        const auto status{audio.read(outputBuf)};
        if (status == AudioPipelineStatus::End) {
            break;
        }
        if (status == AudioPipelineStatus::Error) {
            std::cerr << "[ERROR] Failed to read audio block; aborting." << std::endl;
            dma.stop();
            return 1;
        }

        for (float& sample: outputBuf) {
            sample = nfm.process(sample);
        }
        dma.SetFrequencySamples(outputBuf.data(), outputBuf.size());
    }

    dma.stop();
    std::cout << "pinfm: transmission stopped." << std::endl;
    return 0;
}
