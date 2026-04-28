/**
 * @file piam.cpp
 * @brief Amplitude-modulation (AM) transmitter implementation.
 *
 * Reads audio from a file (any format libsndfile supports), downmixes to
 * mono, resamples to 48 kHz if needed, forms the canonical DSB-FC AM
 * envelope, and drives librpitx::amdmasync directly at the requested
 * carrier frequency. Transmission runs until the audio ends (or forever
 * when --loop is set), or until SIGTERM / SIGINT (the rpitx-ui launcher
 * stops the process centrally via killall when the user dismisses the
 * dialog).
 *
 * @note Usage: piam --freq <Hz> --audio <path> [--loop] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --audio     Path to the audio file (libsndfile-supported format)
 *   - --loop      Loop the audio file (replay from the start on EOF)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "piam.h"

#include <CLI/CLI.hpp>
#include <librpitx/librpitx.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "am_processor.h"
#include "audio_pipeline.h"
#include "cli_common.h"
#include "cli_validators.h"
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

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], AmParameters& params) {
        CLI::App app{"AM transmitter (audio file -> librpitx amdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--audio", params.audioPath, "Input audio file path (libsndfile-supported format)")->required();
        app.add_flag("--loop", params.loop, "Loop the audio file (replay on EOF)");

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        AmParameters params;
        switch (parseArgs(argc, argv, params)) {
            case rpitx::cli::ParseResult::Ok:
                break;
            case rpitx::cli::ParseResult::Help:
                return 0;
            case rpitx::cli::ParseResult::Error:
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

        std::cout << "piam: center=" << params.transmissionFrequency << " Hz, src_rate=" << fmt.sampleRate
                  << " Hz, dma_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
                  << ", format=" << source->description() << ", loop=" << (params.loop ? "yes" : "no") << std::endl;

        AmProcessor am{static_cast<float>(TARGET_SAMPLE_RATE)};
        amdmasync dma{params.transmissionFrequency, TARGET_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

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
