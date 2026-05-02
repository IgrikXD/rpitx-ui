/**
 * @file piam.cpp
 * @brief Amplitude-modulation (AM) transmitter implementation.
 *
 * Reads audio from a file (any format libsndfile supports) or from stdin,
 * downmixes to mono, resamples to 48 kHz if needed, forms the canonical
 * DSB-FC AM envelope, and drives librpitx::amdmasync directly at the
 * requested carrier frequency. Transmission runs until the audio ends (or
 * forever when --loop is set), or until SIGTERM / SIGINT (the rpitx-ui
 * launcher stops the process centrally via killall when the user dismisses
 * the dialog).
 *
 * @note Usage: piam --freq <Hz> (--audio <path> | --stdin) [--loop] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --audio     Path to the audio file (libsndfile-supported format)
 *   - --stdin     Read audio from stdin (pipe-friendly; --loop unsupported)
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

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <exception>
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
        // --audio and --stdin are mutually exclusive and exactly one is
        // required: an option group with require_option(1) lets CLI11 enforce
        // both invariants in one place and surfaces a clean diagnostic in
        // --help, instead of a manual post-parse check that would silently
        // accept neither or both.
        auto* inputGroup{app.add_option_group("input", "Audio input source (exactly one is required)")};
        inputGroup
            ->add_option("--audio", params.audioPath, "Input audio file path (libsndfile-supported format)");
        inputGroup->add_flag("--stdin", params.useStdin, "Read audio from stdin (pipe-friendly; --loop unsupported)");
        inputGroup->require_option(1);
        app.add_flag("--loop", params.loop, "Loop the input on EOF (requires --audio)");

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
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

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);
        // SIGPIPE: stop cleanly when the stdout consumer closes the pipe.
        std::signal(SIGPIPE, handleSignal);

        auto source{makeAudioSource(params.useStdin, params.audioPath)};
        if (source == nullptr) {
            return 1;
        }
        if (validateLoopSupport(*source, params.loop) == false) {
            return 1;
        }

        const auto fmt{source->format()};
        if (validateAudioFormat(fmt, MIN_INPUT_RATE, MAX_INPUT_RATE) == false) {
            return 1;
        }

        // Wrap resource construction and the streaming loop in a try / catch:
        // AudioPipeline (libsoxr handle creation), AmProcessor (Biquad design)
        // and amdmasync (DMA setup) all surface fatal failures as exceptions,
        // and a stray throw escaping main would terminate via std::terminate
        // without flushing the diagnostic the user expects on stderr.
        try {
            AudioPipeline audio{*source,
                                {
                                    .loop               = params.loop,
                                    .targetSampleRate   = TARGET_SAMPLE_RATE,
                                    .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                                    .channelMode        = AudioChannelMode::Mono,
                                }};

            std::cout << "piam: center=" << params.transmissionFrequency << " Hz, src_rate=" << fmt.sampleRate
                      << " Hz, dma_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
                      << ", format=" << source->description() << ", loop=" << (params.loop ? "yes" : "no") << std::endl;

            AmProcessor am{static_cast<float>(TARGET_SAMPLE_RATE)};
            amdmasync dma{
                params.transmissionFrequency, static_cast<uint32_t>(TARGET_SAMPLE_RATE), DMA_BIT_DEPTH, DMA_FIFO_SIZE};

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
                    std::cerr << "[ERROR] piam: failed to read audio block; aborting." << std::endl;
                    dma.stop();
                    return 1;
                }

                for (float& sample: outputBuf) {
                    sample = am.process(sample);
                }
                dma.SetAmSamples(outputBuf.data(), outputBuf.size());
            }

            dma.stop();
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] piam: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "piam: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace piam

int main(int argc, char* argv[]) {
    return piam::run(argc, argv);
}
