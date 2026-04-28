/**
 * @file pinfm.cpp
 * @brief Narrow-band FM (NBFM) transmitter implementation.
 *
 * Reads audio from a file (any format libsndfile supports), downmixes to
 * mono, resamples to 48 kHz if needed, produces a per-sample frequency-
 * deviation stream (+-2.5 kHz narrow or +-5 kHz wide peak), and drives
 * librpitx::ngfmdmasync directly at the requested carrier frequency.
 * Replaces the legacy csdr-based testnfm.sh pipeline, which routed audio
 * through an IQ pipe and gave no bandwidth containment or level control.
 * Transmission runs until the audio ends (or forever when --loop is set),
 * or until SIGTERM / SIGINT (the rpitx-ui launcher stops the process
 * centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pinfm --freq <Hz> --audio <path> [--loop] [--mode narrow|wide] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --audio     Path to the audio file (libsndfile-supported format)
 *   - --loop      Loop the audio file (replay from the start on EOF)
 *   - --mode      NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pinfm.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "audio_pipeline.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "libsndfile_audio_source.h"
#include "nfm_processor.h"

namespace pinfm {
    namespace {
        /**
         * @brief Atomic flag for graceful shutdown on signal reception.
         */
        std::atomic<bool> running{true};
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free for signal-handler access");
    }  // namespace

    float peakDeviationFor(NfmMode mode) {
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

    const char* modeName(NfmMode mode) {
        switch (mode) {
            case NfmMode::Narrow:
                return "narrow";
            case NfmMode::Wide:
                return "wide";
        }
        return "unknown";
    }

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], NfmParameters& params) {
        CLI::App app{"Narrow-band FM transmitter (audio file -> librpitx ngfmdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--audio", params.audioPath, "Input audio file path (libsndfile-supported format)")->required();
        app.add_flag("--loop", params.loop, "Loop the audio file (replay on EOF)");

        const std::map<std::string, NfmMode> modeMap{
            {"narrow", NfmMode::Narrow},
            {"wide", NfmMode::Wide},
        };
        app.add_option("--mode", params.mode, "NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)")
            ->transform(CLI::CheckedTransformer(modeMap, CLI::ignore_case));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        NfmParameters params;
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

        auto source{makeFileAudioSource(params.audioPath)};
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

        const float peakDeviation{peakDeviationFor(params.mode)};

        AudioPipeline audio{*source,
                            {
                                .loop               = params.loop,
                                .targetSampleRate   = TARGET_SAMPLE_RATE,
                                .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                                .tapsPerPhase       = RESAMPLER_TAPS_PER_PHASE,
                                .maxCutoffHz        = RESAMPLER_LPF_CUTOFF,
                                .channelMode        = AudioChannelMode::Mono,
                            }};

        std::cout << "pinfm: center=" << params.transmissionFrequency << " Hz, src_rate=" << fmt.sampleRate
                  << " Hz, dma_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
                  << ", format=" << source->description() << ", mode=" << modeName(params.mode) << " (+-"
                  << peakDeviation << " Hz), loop=" << (params.loop ? "yes" : "no") << std::endl;

        NfmProcessor nfm{static_cast<float>(TARGET_SAMPLE_RATE), peakDeviation};
        ngfmdmasync dma{params.transmissionFrequency, static_cast<uint32_t>(TARGET_SAMPLE_RATE), DMA_BIT_DEPTH,
                        DMA_FIFO_SIZE};

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
}  // namespace pinfm

int main(int argc, char* argv[]) {
    return pinfm::run(argc, argv);
}
