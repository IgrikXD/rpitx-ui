/**
 * @file piam.cpp
 * @brief Amplitude-modulation (AM) transmitter implementation.
 *
 * Reads audio from a file (any format libsndfile supports), downmixes to
 * mono, resamples to 48 kHz if needed, forms the canonical DSB-FC AM
 * envelope, and drives librpitx::amdmasync directly at the requested
 * carrier frequency. Transmission runs until the audio ends (or forever
 * when -l is set), or until SIGTERM / SIGINT (the rpitx-ui launcher stops
 * the process centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: piam <freq_Hz> -a <file> [-l] [-h]
 *   - -a   Path to the audio file (any format libsndfile understands).
 *   - -l   Loop the audio file (replay from the start on EOF).
 *   - -h   Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "piam.h"

#include <librpitx/librpitx.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <optional>
#include <vector>

#include "am_processor.h"
#include "audio_pipeline.h"
#include "libsndfile_audio_source.h"

namespace piam {
    namespace {
        /**
         * @brief Atomic flag for graceful shutdown on signal reception.
         *
         * Must be lock-free to be safe to touch from a signal handler; statically
         * asserted below to fail fast on any exotic platform where it is not.
         */
        std::atomic<bool> running{true};
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free for signal-handler access");
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    void printUsage() {
        std::cerr << "Usage: piam <freq_Hz> -a <file> [options]" << std::endl
                  << "  -a <file>  Path to the audio file (libsndfile-supported format)" << std::endl
                  << "  -l         Loop the audio file (replay on EOF)" << std::endl
                  << "  -h         Print this help message" << std::endl
                  << "  Audio is downmixed to mono and resampled to " << TARGET_SAMPLE_RATE << " Hz internally."
                  << std::endl;
    }

    ParseResult parsePositionalArgs(std::string_view freqArg, AmParameters& params) {
        const auto freqOpt{parseNumericArg<double>(freqArg)};
        if (freqOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid frequency argument!" << std::endl;
            return ParseResult::Error;
        }
        // Guard the double -> uint64_t conversion. UINT64_MAX (2^64 - 1) is not
        // exactly representable as double; std::ldexp(1.0, 64) is exactly 2^64
        // and is the strict upper bound any finite double can convert from.
        const double freqValue{freqOpt.value()};
        if (freqValue <= 0.0 || std::isfinite(freqValue) == false || freqValue >= std::ldexp(1.0, 64)) {
            std::cerr << "[ERROR] Frequency is out of representable range!" << std::endl;
            return ParseResult::Error;
        }
        params.freq = static_cast<uint64_t>(freqValue);
        return ParseResult::Ok;
    }

    ParseResult parseOptionalFlags(std::span<char* const> args, AmParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            if (arg == "-l") {
                params.loop = true;
                continue;
            }
            if (arg != "-a") {
                std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
                return ParseResult::Error;
            }
            if (++i >= args.size()) {
                std::cerr << "[ERROR] Option " << arg << " requires an argument!" << std::endl;
                return ParseResult::Error;
            }
            params.audioPath.assign(args[i]);
        }
        return ParseResult::Ok;
    }

    ParseResult parseArgs(int argc, char* argv[], AmParameters& params) {
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

    int run(int argc, char* argv[]) {
        AmParameters params;
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
        // piam writes startup/shutdown status to stdout. If stdout is attached
        // to a pipe whose reader exits (e.g. `piam ... | tee log` where tee is
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

        AudioPipeline audio{*source,
                            {
                                .loop               = params.loop,
                                .targetSampleRate   = static_cast<int>(TARGET_SAMPLE_RATE),
                                .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                                .tapsPerPhase       = RESAMPLER_TAPS_PER_PHASE,
                                .maxCutoffHz        = RESAMPLER_LPF_CUTOFF,
                                .channelMode        = AudioChannelMode::Mono,
                            }};

        std::cout << "piam: center=" << params.freq << " Hz, src_rate=" << fmt.sampleRate
                  << " Hz, dma_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
                  << ", format=" << source->description() << ", loop=" << (params.loop ? "yes" : "no") << std::endl;

        AmProcessor am{static_cast<float>(TARGET_SAMPLE_RATE)};
        amdmasync dma{params.freq, TARGET_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

        // AudioPipeline owns source-rate input buffers, downmixing, loop-aware
        // EOF handling, and source -> 48 kHz rate conversion. outputBuf is reused
        // for the AM envelope in place before handing the block to DMA.
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
                sample = am.process(sample);
            }
            dma.SetAmSamples(outputBuf.data(), outputBuf.size());
        }

        dma.stop();
        std::cout << "piam: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace piam

int main(int argc, char* argv[]) {
    return piam::run(argc, argv);
}
