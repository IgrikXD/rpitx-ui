/**
 * @file pissb.cpp
 * @brief Single-sideband (SSB) transmitter implementation.
 *
 * Reads audio from a file (any format libsndfile supports), downmixes to
 * mono, resamples to 48 kHz if needed, applies SSB modulation (USB or LSB),
 * and drives librpitx::iqdmasync directly at the requested carrier frequency.
 * Transmission runs until the audio ends (or forever when --loop is set), or
 * until SIGTERM / SIGINT.
 *
 * @note Usage: pissb --freq <Hz> --audio <path> [--loop] [--sideband usb|lsb] [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --audio     Path to the audio file (libsndfile-supported format)
 *   - --loop      Loop the audio file (replay from the start on EOF)
 *   - --sideband  Sideband selection: usb (default) | lsb
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pissb.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <atomic>
#include <complex>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "audio_pipeline.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "libsndfile_audio_source.h"

namespace pissb {
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

    const char* modeName(SsbMode mode) {
        switch (mode) {
            case SsbMode::USB:
                return "usb";
            case SsbMode::LSB:
                return "lsb";
        }
        return "unknown";
    }

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PissbParameters& params) {
        CLI::App app{"SSB transmitter (audio file -> librpitx iqdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--audio", params.audioPath, "Input audio file path (libsndfile-supported format)")->required();
        app.add_flag("--loop", params.loop, "Loop the audio file (replay on EOF)");

        const std::map<std::string, SsbMode> sidebandMap{
            {"usb", SsbMode::USB},
            {"lsb", SsbMode::LSB},
        };
        // Override CheckedTransformer's auto-description: enum class values stream as empty,
        // which would otherwise render as "ENUM:value in {usb->,lsb->} OR {,}" in --help.
        app.add_option("--sideband", params.mode, "Sideband selection: usb (default) | lsb")
            ->transform(CLI::CheckedTransformer(sidebandMap, CLI::ignore_case).description("usb|lsb"));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        PissbParameters params;
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
        // SIGPIPE: keep shutdown behavior explicit when launched from shell wrappers.
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

        AudioPipeline audio{*source,
                            {
                                .loop               = params.loop,
                                .targetSampleRate   = TARGET_SAMPLE_RATE,
                                .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                                .tapsPerPhase       = RESAMPLER_TAPS_PER_PHASE,
                                .maxCutoffHz        = RESAMPLER_LPF_CUTOFF,
                                .channelMode        = AudioChannelMode::Mono,
                            }};

        std::cout << "pissb: center=" << params.transmissionFrequency << " Hz, src_rate=" << fmt.sampleRate
                  << " Hz, iq_rate=" << TARGET_SAMPLE_RATE << " Hz, channels=" << fmt.channels
                  << ", format=" << source->description() << ", sideband=" << modeName(params.mode)
                  << ", loop=" << (params.loop ? "yes" : "no") << std::endl;

        SsbProcessor ssb{params.mode};
        iqdmasync dma{params.transmissionFrequency,
                      static_cast<uint32_t>(TARGET_SAMPLE_RATE),
                      DMA_BIT_DEPTH,
                      DMA_FIFO_SIZE,
                      MODE_IQ};
        dma.SetPLLMasterLoop(3, 4, 0);

        // AudioPipeline owns source-rate input buffers, downmixing, loop-aware
        // EOF handling, and source -> 48 kHz rate conversion. Each mono sample
        // is converted into one complex IQ sample before being handed to DMA.
        std::vector<float> audioBuf(audio.outputSamplesPerBlock());
        std::vector<std::complex<float>> iqBuf(static_cast<std::size_t>(audio.outputFrames()));

        while (running.load(std::memory_order_relaxed)) {
            const auto status{audio.read(audioBuf)};
            if (status == AudioPipelineStatus::End) {
                break;
            }
            if (status == AudioPipelineStatus::Error) {
                std::cerr << "[ERROR] Failed to read audio block; aborting." << std::endl;
                dma.stop();
                return 1;
            }

            for (std::size_t i{0}; i < iqBuf.size(); ++i) {
                const auto iq{ssb.process(audioBuf[i])};
                iqBuf[i] = std::complex<float>{iq.i, iq.q};
            }
            dma.SetIQSamples(iqBuf.data(), iqBuf.size(), DEFAULT_HARMONIC);
        }

        dma.stop();
        std::cout << "pissb: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pissb

int main(int argc, char* argv[]) {
    return pissb::run(argc, argv);
}
