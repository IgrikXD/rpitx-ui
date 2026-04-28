/**
 * @file pifmrds.cpp
 * @brief Wide-band FM with RDS broadcast transmitter implementation.
 *
 * Reads audio (mono or stereo, any rate / bit-depth supported by libsndfile)
 * from a file specified via --audio, builds the FM broadcast MPX (audio +
 * 19 kHz pilot + 38 kHz suppressed-carrier (L-R) subcarrier when stereo +
 * 57 kHz RDS subcarrier with EN 50067 PI / PS / RT / CT data), and drives
 * librpitx::ngfmdmasync at 228 kHz to produce the on-air signal.
 * Transmission runs until the audio ends (or forever when --loop is set),
 * or until SIGTERM / SIGINT (the rpitx-ui launcher stops the process
 * centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pifmrds --freq <Hz> --audio <path> [--loop]
 *               [--rds-pi <hex>] [--rds-ps <text>] [--rds-rt <text>]
 *               [--pre-emphasis 50|75] [-h | --help]
 *   - --freq          Carrier frequency in Hz
 *   - --audio         Path to the audio file (libsndfile-supported format)
 *   - --loop          Loop the audio file (replay from the start on EOF)
 *   - --rds-pi        RDS Programme Identification, 1-4 hex digits (default 0x1234)
 *   - --rds-ps        RDS Programme Service name, 1-8 bytes (default "rpitx-ui")
 *   - --rds-rt        RDS RadioText, 1-64 bytes
 *   - --pre-emphasis  FM pre-emphasis in microseconds: 50 | 75 (default 50)
 *   - -h, --help      Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pifmrds.h"

#include <CLI/CLI.hpp>
#include <librpitx/librpitx.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "audio_pipeline.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "fmrds_processor.h"
#include "libsndfile_audio_source.h"
#include "rds_encoder.h"

namespace pifmrds {
    namespace {
        /**
         * @brief Atomic flag for graceful shutdown on signal reception.
         */
        std::atomic<bool> running{true};
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free for signal-handler access");

        /**
         * @brief Parse 1-4 hex digits (with optional 0x prefix) into uint16_t.
         *
         * Local to pifmrds - the RDS PI code format is unique to this binary
         * and not worth factoring into the shared validator layer.
         */
        [[nodiscard]] bool parseRdsPi(std::string_view text, uint16_t& out) {
            std::string_view body{text};
            if (body.size() >= 2 && (body.substr(0, 2) == "0x" || body.substr(0, 2) == "0X")) {
                body.remove_prefix(2);
            }
            if (body.empty() || body.size() > 4) {
                return false;
            }
            uint16_t value{};
            for (const char c: body) {
                int digit{};
                if (c >= '0' && c <= '9') {
                    digit = c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    digit = 10 + (c - 'a');
                } else if (c >= 'A' && c <= 'F') {
                    digit = 10 + (c - 'A');
                } else {
                    return false;
                }
                value = static_cast<uint16_t>((value << 4) | digit);
            }
            out = value;
            return true;
        }
    }  // namespace

    float preEmphasisTauFor(PreEmphasisMode mode) {
        switch (mode) {
            case PreEmphasisMode::Eu50:
                return 50e-6F;
            case PreEmphasisMode::Us75:
                return 75e-6F;
        }
        return 0.0F;
    }

    const char* preEmphasisName(PreEmphasisMode mode) {
        switch (mode) {
            case PreEmphasisMode::Eu50:
                return "50";
            case PreEmphasisMode::Us75:
                return "75";
        }
        return "unknown";
    }

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FmRdsParameters& params) {
        CLI::App app{"FM broadcast transmitter with RDS (audio file -> librpitx ngfmdmasync at 228 kHz)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--audio", params.audioPath, "Input audio file path (libsndfile-supported format)")->required();
        app.add_flag("--loop", params.loop, "Loop the audio file (replay on EOF)");

        std::string piText;
        app.add_option("--rds-pi", piText, "RDS Programme Identification, 1-4 hex digits (default 1234)");

        // PS / RT use byte-length validation (size() in bytes, not user-perceived
        // characters): the RDS encoder copies up to PS_LENGTH / RT_LENGTH bytes
        // into fixed-size arrays, so byte length is the meaningful unit here.
        app.add_option("--rds-ps", params.ps,
                       "RDS Programme Service name, 1-8 bytes (default \"rpitx-ui\")")
            ->check(CLI::Validator{
                [](const std::string& text) -> std::string {
                    if (text.empty()) {
                        return "must not be empty";
                    }
                    if (text.size() > RdsEncoder::PS_LENGTH) {
                        return "must be at most 8 bytes";
                    }
                    return {};
                },
                "RDS PS, 1-8 bytes", "PS_BYTES"});
        app.add_option("--rds-rt", params.rt,
                       "RDS RadioText, 1-64 bytes (default \"rpitx-ui Broadcast WFM with RDS\")")
            ->check(CLI::Validator{
                [](const std::string& text) -> std::string {
                    if (text.empty()) {
                        return "must not be empty";
                    }
                    if (text.size() > RdsEncoder::RT_LENGTH) {
                        return "must be at most 64 bytes";
                    }
                    return {};
                },
                "RDS RT, 1-64 bytes", "RT_BYTES"});

        const std::map<std::string, PreEmphasisMode> preEmphMap{
            {"50", PreEmphasisMode::Eu50},
            {"75", PreEmphasisMode::Us75},
        };
        app.add_option("--pre-emphasis", params.preEmph, "FM pre-emphasis in microseconds: 50 (default) | 75")
            ->transform(CLI::CheckedTransformer(preEmphMap));

        if (const auto result{rpitx::cli::finalizeParse(app, argc, argv)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (const auto result{rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // --rds-pi was captured as a string so we can validate the 1-4 hex
        // digit format with a meaningful diagnostic; the default is left in
        // place when the option is not supplied.
        if (piText.empty() == false) {
            if (parseRdsPi(piText, params.pi) == false) {
                std::cerr << "[ERROR] Invalid --rds-pi: '" << piText << "' (expected 1-4 hex digits)" << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
        }

        return rpitx::cli::ParseResult::Ok;
    }

    int run(int argc, char* argv[]) {
        FmRdsParameters params;
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
        // pifmrds writes startup/shutdown status to stdout. If stdout is attached
        // to a pipe whose reader exits (e.g. `pifmrds ... | tee log` where tee is
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

        const auto audioFormat{source->format()};
        if (validateMonoStereoAudioFormat(audioFormat, 8'000, MPX_SAMPLE_RATE) == false) {
            return 1;
        }

        AudioPipeline audio{*source,
                            {
                                .loop               = params.loop,
                                .targetSampleRate   = MPX_SAMPLE_RATE,
                                .targetOutputFrames = TARGET_OUTPUT_FRAMES,
                                .tapsPerPhase       = RESAMPLER_TAPS_PER_PHASE,
                                .maxCutoffHz        = RESAMPLER_LPF_CUTOFF,
                                .channelMode        = AudioChannelMode::Preserve,
                            }};

        std::cout << "pifmrds: center=" << params.transmissionFrequency << " Hz, audio_rate=" << audioFormat.sampleRate
                  << " Hz, channels=" << audioFormat.channels << ", mpx_rate=" << MPX_SAMPLE_RATE
                  << " Hz, format=" << source->description() << ", loop=" << (params.loop ? "yes" : "no") << std::endl
                  << "         PI=0x" << std::hex << params.pi << std::dec << ", PS=\"" << params.ps << "\", RT=\""
                  << params.rt << "\", pre-emph=" << preEmphasisName(params.preEmph) << " us" << std::endl;

        FmRdsProcessor proc{{
            .audioSampleRate = MPX_SAMPLE_RATE,
            .channels        = audio.outputChannels(),
            .mpxSampleRate   = MPX_SAMPLE_RATE,
            .peakDeviation   = PEAK_DEVIATION,
            .preEmphasisTau  = preEmphasisTauFor(params.preEmph),
        }};
        proc.encoder().setPi(params.pi);
        proc.encoder().setPs(params.ps);
        proc.encoder().setRt(params.rt);

        ngfmdmasync dma{params.transmissionFrequency, MPX_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

        // AudioPipeline owns source-rate input buffers, loop-aware EOF handling,
        // channel preservation, and source -> 228 kHz rate conversion. The FM-RDS
        // processor receives one already-rate-matched audio frame per MPX sample.
        std::vector<float> audioBuf(audio.outputSamplesPerBlock());
        std::vector<float> mpxBuf(static_cast<std::size_t>(audio.outputFrames()));

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
            proc.process({audioBuf.data(), audioBuf.size()}, {mpxBuf.data(), mpxBuf.size()});
            dma.SetFrequencySamples(mpxBuf.data(), mpxBuf.size());
        }

        dma.stop();
        std::cout << "pifmrds: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pifmrds

int main(int argc, char* argv[]) {
    return pifmrds::run(argc, argv);
}
