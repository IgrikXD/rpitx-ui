/**
 * @file pifmrds.cpp
 * @brief Wide-band FM with RDS broadcast transmitter implementation.
 *
 * Reads audio (mono or stereo, any rate / bit-depth supported by libsndfile)
 * from a file specified via -a, builds the FM broadcast MPX (audio + 19 kHz
 * pilot + 38 kHz suppressed-carrier (L-R) subcarrier when stereo + 57 kHz
 * RDS subcarrier with EN 50067 PI / PS / RT / CT data), and drives
 * librpitx::ngfmdmasync at 228 kHz to produce the on-air signal. Transmission
 * runs until the audio ends (or forever when -l is set), or until SIGTERM /
 * SIGINT (the rpitx-ui launcher stops the process centrally via killall when
 * the user dismisses the dialog).
 *
 * @note Usage: pifmrds <freq_Hz> -a <file> [-l] [-pi <hex>] [-ps <text>] [-rt <text>] [-pe <50|75>] [-h]
 *   - -a   Path to the audio file (any format libsndfile understands).
 *   - -l   Loop the audio file (replay from the start on EOF).
 *   - -pi  RDS Programme Identification, 4 hex digits (default: 0x1234)
 *   - -ps  RDS Programme Service name, up to 8 chars (default: "rpitx-ui")
 *   - -rt  RDS RadioText, up to 64 chars (default: "rpitx-ui Broadcast WFM with RDS")
 *   - -pe  Pre-emphasis: 50 (us, ITU regions 1/3) | 75 (us, region 2 / Japan)
 *          (default: 50)
 *   - -h   Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pifmrds.h"

#include <librpitx/librpitx.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <optional>
#include <vector>

#include "audio_pipeline.h"
#include "fmrds_processor.h"
#include "libsndfile_audio_source.h"

namespace pifmrds {
    namespace {
        /**
         * @brief Atomic flag for graceful shutdown on signal reception.
         */
        std::atomic<bool> running{true};
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free for signal-handler access");
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

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    void printUsage() {
        std::cerr << "Usage: pifmrds <freq_Hz> -a <file> [options]" << std::endl
                  << "  -a <file>   Path to the audio file (libsndfile-supported format)" << std::endl
                  << "  -l          Loop the audio file (replay on EOF)" << std::endl
                  << "  -pi <hex>   RDS Programme Identification, 4 hex digits (default 0x1234)" << std::endl
                  << "  -ps <text>  RDS Programme Service name, up to 8 chars (default \"rpitx-ui\")" << std::endl
                  << "  -rt <text>  RDS RadioText, up to 64 chars" << std::endl
                  << "  -pe <50|75> Pre-emphasis time constant in microseconds (default 50)" << std::endl
                  << "  -h          Print this help message" << std::endl;
    }

    ParseResult parsePositionalArgs(std::string_view freqArg, FmRdsParameters& params) {
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

    ParseResult parsePi(std::string_view arg, uint16_t& out) {
        std::string_view body{arg};
        if (body.size() >= 2 && (body.substr(0, 2) == "0x" || body.substr(0, 2) == "0X")) {
            body.remove_prefix(2);
        }
        if (body.empty() || body.size() > 4) {
            return reportInvalidValue("PI code", arg);
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
                return reportInvalidValue("PI code", arg);
            }
            value = static_cast<uint16_t>((value << 4) | digit);
        }
        out = value;
        return ParseResult::Ok;
    }

    ParseResult parseOptionalFlags(std::span<char* const> args, FmRdsParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            // -l is a presence-only boolean and so is parsed separately; the
            // remaining flags all consume one value argument.
            if (arg == "-l") {
                params.loop = true;
                continue;
            }

            // Reject unknown flags before consuming a value, so that a trailing unknown flag
            // surfaces as "Unknown option" instead of the misleading "Option -x requires an argument".
            if (arg != "-a" && arg != "-pi" && arg != "-ps" && arg != "-rt" && arg != "-pe") {
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
            } else if (arg == "-pi") {
                if (const auto result{parsePi(value, params.pi)}; result != ParseResult::Ok) {
                    return result;
                }
            } else if (arg == "-ps") {
                if (value.size() > RdsEncoder::PS_LENGTH) {
                    std::cerr << "[ERROR] PS exceeds " << RdsEncoder::PS_LENGTH << " characters!" << std::endl;
                    return ParseResult::Error;
                }
                params.ps = value;
            } else if (arg == "-rt") {
                if (value.size() > RdsEncoder::RT_LENGTH) {
                    std::cerr << "[ERROR] RT exceeds " << RdsEncoder::RT_LENGTH << " characters!" << std::endl;
                    return ParseResult::Error;
                }
                params.rt = value;
            } else if (arg == "-pe") {
                const auto mode{parseNamedEnum(value, PRE_EMPH_TABLE)};
                if (mode == std::nullopt) {
                    std::cerr << "[ERROR] Unknown pre-emphasis '" << value << "'. Expected 50 | 75." << std::endl;
                    return ParseResult::Error;
                }
                params.preEmph = mode.value();
            }
        }
        return ParseResult::Ok;
    }

    ParseResult parseArgs(int argc, char* argv[], FmRdsParameters& params) {
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
        FmRdsParameters params;
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

        std::cout << "pifmrds: center=" << params.freq << " Hz, audio_rate=" << audioFormat.sampleRate
                  << " Hz, channels=" << audioFormat.channels << ", mpx_rate=" << MPX_SAMPLE_RATE
                  << " Hz, format=" << source->description() << ", loop=" << (params.loop ? "yes" : "no") << std::endl
                  << "         PI=0x" << std::hex << params.pi << std::dec << ", PS=\"" << params.ps << "\", RT=\""
                  << params.rt << "\", pre-emph=" << formatNamedEnum(params.preEmph, PRE_EMPH_TABLE) << " us"
                  << std::endl;

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

        ngfmdmasync dma{params.freq, MPX_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

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
