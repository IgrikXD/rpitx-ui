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
 *   - --rds-ps        RDS Programme Service name, 1-8 ASCII chars (default "rpitx-ui")
 *   - --rds-rt        RDS RadioText, 1-64 ASCII chars
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

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
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

        /**
         * @brief Build a CLI11 validator for RDS PS / RT text fields.
         *
         * RDS PS/RT use the EN 50067 G0 character set, not UTF-8: bytes
         * outside printable ASCII (0x20-0x7E) would render as garbage on
         * receivers and also overflow the fixed-length PS/RT fields when
         * a multi-byte encoding is used. Validating bytes consistently here
         * keeps CLI behaviour aligned with the bash UI's character checks
         * once both sides agree the input is ASCII.
         */
        [[nodiscard]] CLI::Validator makeRdsTextValidator(int maxBytes, std::string fieldName) {
            auto check{[maxBytes](const std::string& text) -> std::string {
                if (text.empty()) {
                    return "must not be empty";
                }
                if (text.size() > static_cast<std::size_t>(maxBytes)) {
                    return "must be at most " + std::to_string(maxBytes) + " ASCII characters";
                }
                for (const char ch: text) {
                    const auto byte{static_cast<unsigned char>(ch)};
                    if (byte < 0x20 || byte > 0x7E) {
                        return "must contain only printable ASCII (0x20-0x7E); "
                               "RDS does not carry non-ASCII text";
                    }
                }
                return {};
            }};
            auto description{"RDS " + fieldName + ", 1-" + std::to_string(maxBytes) + " ASCII chars"};
            auto name{std::move(fieldName) + "_ASCII"};
            return CLI::Validator{std::move(check), std::move(description), std::move(name)};
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

        app.add_option("--rds-ps", params.ps, "RDS Programme Service name, 1-8 ASCII chars (default \"rpitx-ui\")")
            ->check(makeRdsTextValidator(RdsEncoder::PS_LENGTH, "PS"));
        app.add_option("--rds-rt", params.rt,
                       "RDS RadioText, 1-64 ASCII chars (default \"rpitx-ui Broadcast WFM with RDS\")")
            ->check(makeRdsTextValidator(RdsEncoder::RT_LENGTH, "RT"));

        const std::map<std::string, PreEmphasisMode> preEmphMap{
            {"50", PreEmphasisMode::Eu50},
            {"75", PreEmphasisMode::Us75},
        };
        // Override CheckedTransformer's auto-description: enum class values stream as empty,
        // which would otherwise render as "ENUM:value in {50->,75->} OR {,}" in --help.
        app.add_option("--pre-emphasis", params.preEmph, "FM pre-emphasis in microseconds: 50 (default) | 75")
            ->transform(CLI::CheckedTransformer(preEmphMap).description("50|75"));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
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

        const auto audioFormat{source->format()};
        if (validateAudioFormat(audioFormat, MIN_INPUT_RATE, MAX_INPUT_RATE) == false) {
            return 1;
        }

        AudioPipeline audio{*source,
                            {
                                .loop               = params.loop,
                                .targetSampleRate   = MPX_SAMPLE_RATE,
                                .targetOutputFrames = TARGET_OUTPUT_FRAMES,
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

        ngfmdmasync dma{params.transmissionFrequency, static_cast<uint32_t>(MPX_SAMPLE_RATE), DMA_BIT_DEPTH,
                        DMA_FIFO_SIZE};

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
