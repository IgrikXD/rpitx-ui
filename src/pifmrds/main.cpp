/**
 * @file main.cpp
 * @brief Wide-band FM with RDS broadcast transmitter.
 *
 * Reads 16-bit PCM audio (mono or stereo, any sample rate) from stdin,
 * builds the FM broadcast MPX (audio + 19 kHz pilot + 38 kHz suppressed-
 * carrier (L-R) subcarrier when stereo + 57 kHz RDS subcarrier with
 * EN 50067 PI / PS / RT / CT data), and drives librpitx::ngfmdmasync at
 * 228 kHz to produce the on-air signal. Transmission runs until SIGTERM /
 * SIGINT (the rpitx-ui launcher stops the process centrally via killall
 * when the user dismisses the dialog).
 *
 * @note Usage: pifmrds <freq_Hz> [-pi <hex>] [-ps <text>] [-rt <text>] [-pe <50|75>] [-h]
 *   - -pi  RDS Programme Identification, 4 hex digits (default: 0x1234)
 *   - -ps  RDS Programme Service name, up to 8 chars (default: "rpitx-ui")
 *   - -rt  RDS RadioText, up to 64 chars (default: "rpitx-ui Broadcast WFM with RDS")
 *   - -pe  Pre-emphasis: 50 (us, ITU regions 1/3) | 75 (us, region 2 / Japan)
 *          (default: 50)
 *   - -h   Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.04.2026
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
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "cli_utils.h"
#include "fmrds_processor.h"
#include "wav_utils.h"

namespace {
    /**
     * @brief DMA sample buffer depth.
     */
    constexpr uint32_t DMA_FIFO_SIZE{16384};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief MPX / DMA sample rate in Hz.
     *
     * 228 kHz is the canonical FM broadcast MPX rate: it places the 57 kHz
     * RDS subcarrier at exactly Fs/4, the 19 kHz pilot at Fs/12, and the
     * 38 kHz stereo subcarrier at Fs/6 - all integer phase ratios, so the
     * sine modulators collapse to small lookup tables rather than NCOs.
     */
    constexpr int MPX_SAMPLE_RATE{228'000};

    /**
     * @brief Default audio sample rate when no RIFF header is present.
     *
     * skipWavHeader returns sampleRate == 0 for raw PCM streams (no RIFF
     * detected); we fall back to this value rather than aborting because
     * raw 48 kHz PCM is a common pipeline format and any genuine mismatch
     * just results in audible pitch shift, not a transmission failure.
     */
    constexpr int DEFAULT_AUDIO_SAMPLE_RATE{48'000};

    /**
     * @brief Peak FM deviation in Hz.
     *
     * EN 50067 / ITU-R BS.450: 75 kHz peak deviation is the maximum allowed
     * on a 200 kHz FM broadcast channel. We do not expose this on the CLI
     * because narrow-deviation operation is the role of pinfm.
     */
    constexpr float PEAK_DEVIATION{75'000.0F};

    /**
     * @brief Normalization divisor for int16_t -> float [-1.0, 1.0] conversion (2^15).
     */
    constexpr float PCM16_MAX{static_cast<float>(std::numeric_limits<int16_t>::max()) + 1.0f};

    /**
     * @brief Default RDS Programme Identification code.
     *
     * 0x1234 is the canonical PiFmRds default and a non-broadcasted PI value
     * in the EN 50067 country tables, so receivers tuning into a test
     * transmission do not get confused with a real station's identity.
     */
    constexpr uint16_t DEFAULT_RDS_PI{0x1234};

    /**
     * @brief Default RDS Programme Service (PS) name.
     */
    constexpr std::string_view DEFAULT_RDS_PS{"rpitx-ui"};

    /**
     * @brief Default RDS RadioText (RT).
     */
    constexpr std::string_view DEFAULT_RDS_RT{"rpitx-ui Broadcast WFM with RDS"};

    /**
     * @brief Pre-emphasis time-constant preset.
     *
     * Closed-set of the two standardised broadcast pre-emphasis time
     * constants:
     *   - Eu50: 50 us, ITU regions 1 / 3 (Europe, Africa, Asia, Oceania)
     *   - Us75: 75 us, ITU region 2 (Americas) and Japan
     */
    enum class PreEmphasisMode : uint8_t {
        Eu50,
        Us75,
    };

    /**
     * @brief PreEmphasisMode textual names for CLI parsing and display.
     */
    constexpr std::array PRE_EMPH_TABLE{
        NamedEnum<PreEmphasisMode>{"50", PreEmphasisMode::Eu50},
        NamedEnum<PreEmphasisMode>{"75", PreEmphasisMode::Us75},
    };

    /**
     * @brief Map a pre-emphasis preset to its time constant in seconds.
     * @param mode Selected pre-emphasis preset.
     * @return Time constant in seconds.
     */
    [[nodiscard]] constexpr float preEmphasisTauFor(PreEmphasisMode mode) {
        switch (mode) {
            case PreEmphasisMode::Eu50:
                return 50e-6F;
            case PreEmphasisMode::Us75:
                return 75e-6F;
        }
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
        std::cerr << "Usage: pifmrds <freq_Hz> [options]" << std::endl
                  << "  -pi <hex>   RDS Programme Identification, 4 hex digits (default 0x1234)" << std::endl
                  << "  -ps <text>  RDS Programme Service name, up to 8 chars (default \"rpitx-ui\")" << std::endl
                  << "  -rt <text>  RDS RadioText, up to 64 chars" << std::endl
                  << "  -pe <50|75> Pre-emphasis time constant in microseconds (default 50)" << std::endl
                  << "  -h          Print this help message" << std::endl
                  << "  Reads 16-bit PCM audio (mono or stereo, any rate) from stdin." << std::endl;
    }

    /**
     * @brief FM-RDS parameters extracted from argv.
     */
    struct FmRdsParameters {
        uint64_t freq{0};
        uint16_t pi{DEFAULT_RDS_PI};
        std::string_view ps{DEFAULT_RDS_PS};
        std::string_view rt{DEFAULT_RDS_RT};
        PreEmphasisMode preEmph{PreEmphasisMode::Eu50};
    };

    /**
     * @brief Parse and validate the single positional argument (frequency).
     */
    [[nodiscard]] ParseResult parsePositionalArgs(std::string_view freqArg, FmRdsParameters& params) {
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

    /**
     * @brief Parse a hex PI code from a CLI argument.
     *
     * Accepts plain hex digits (e.g. "FFFF") and 0x-prefixed (e.g. "0xFFFF").
     * Rejects empty strings, non-hex characters, and out-of-range values.
     *
     * @param arg CLI argument to parse.
     * @param out Output - assigned the parsed PI on success.
     * @return ParseResult::Ok on success, ::Error otherwise.
     */
    [[nodiscard]] ParseResult parsePi(std::string_view arg, uint16_t& out) {
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

    /**
     * @brief Walk the optional flag tail and populate params.
     */
    [[nodiscard]] ParseResult parseOptionalFlags(std::span<char* const> args, FmRdsParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            // Reject unknown flags before consuming a value, so that a trailing unknown flag
            // surfaces as "Unknown option" instead of the misleading "Option -x requires an argument".
            if (arg != "-pi" && arg != "-ps" && arg != "-rt" && arg != "-pe") {
                std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
                return ParseResult::Error;
            }
            if (++i >= args.size()) {
                std::cerr << "[ERROR] Option " << arg << " requires an argument!" << std::endl;
                return ParseResult::Error;
            }
            const std::string_view value{args[i]};

            if (arg == "-pi") {
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

    /**
     * @brief Parse and validate command-line arguments.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], FmRdsParameters& params) {
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
        return ParseResult::Ok;
    }

    /**
     * @brief Read one block of audio frames from stdin into the float scratch.
     *
     * Reads exactly `proc.audioFramesPerBlock()` frames; on a short read at
     * end-of-stream the remaining slots are zero-padded so the resampler's
     * `inSize % M == 0` invariant is preserved (any tail less than one
     * block is rounded up rather than dropped).
     *
     * @param channels Channel count from the WAV header (1 or 2).
     * @param frames   Frames to read (== proc.audioFramesPerBlock()).
     * @param pcmBuf   int16 scratch sized at frames * channels.
     * @param outBuf   Output float buffer sized at frames * channels.
     *                 For stereo, samples are interleaved L, R, L, R, ...
     * @return Number of full audio frames actually read; 0 on EOF.
     */
    [[nodiscard]] int readAudioBlock(int channels, int frames, std::vector<int16_t>& pcmBuf,
                                     std::vector<float>& outBuf) {
        const auto framesRead{static_cast<int>(std::fread(pcmBuf.data(),
                                                          sizeof(int16_t) * static_cast<std::size_t>(channels),
                                                          static_cast<std::size_t>(frames),
                                                          stdin))};
        if (framesRead <= 0) {
            return 0;
        }
        const std::size_t samplesRead{static_cast<std::size_t>(framesRead) * static_cast<std::size_t>(channels)};
        for (std::size_t i{0}; i < samplesRead; ++i) {
            outBuf[i] = static_cast<float>(pcmBuf[i]) / PCM16_MAX;
        }
        // Zero-pad the tail so the block is always exactly `frames` frames -
        // resampler invariant + avoids leaving a partial trailing-frame
        // block undelivered when the input stream runs short.
        const std::size_t fullSamples{static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels)};
        for (std::size_t i{samplesRead}; i < fullSamples; ++i) {
            outBuf[i] = 0.0F;
        }
        return framesRead;
    }
}  // namespace

int main(int argc, char* argv[]) {
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

    // Skip the WAV header exactly once at stream start. The easytest.sh
    // pipeline (`while true; do cat $file; done`) holds the write end of
    // the pipe open across cat iterations, so fread never returns a
    // partial read at a file boundary - any follow-up RIFF headers land
    // mid-block and cannot be recovered from partial-read detection.
    const auto header{skipWavHeader()};
    if (header == std::nullopt) {
        std::cerr << "pifmrds: stdin closed before any audio was read." << std::endl;
        return 1;
    }
    const auto& info{header.value()};

    if (info.channels != 1 && info.channels != 2) {
        std::cerr << "[ERROR] Input must be mono or stereo, got " << info.channels << " channels." << std::endl;
        return 1;
    }
    if (info.bitsPerSample != 16) {
        std::cerr << "[ERROR] Input must be 16-bit PCM, got " << info.bitsPerSample << "-bit." << std::endl;
        return 1;
    }
    // sampleRate == 0 means "no RIFF header was found" - fall back to a
    // sensible default (assumes the user knows their raw stream). For
    // anything that looks like a sane PCM rate we trust the WAV header
    // and let the polyphase resampler adjust.
    int audioRate{DEFAULT_AUDIO_SAMPLE_RATE};
    if (info.sampleRate > 0) {
        audioRate = info.sampleRate;
    }
    if (audioRate < 8'000 || audioRate > MPX_SAMPLE_RATE) {
        std::cerr << "[ERROR] Input sample rate " << audioRate << " Hz is outside the supported range [8000, "
                  << MPX_SAMPLE_RATE << "]." << std::endl;
        return 1;
    }

    std::cout << "pifmrds: center=" << params.freq << " Hz, audio_rate=" << audioRate
              << " Hz, channels=" << info.channels << ", mpx_rate=" << MPX_SAMPLE_RATE << " Hz" << std::endl
              << "         PI=0x" << std::hex << params.pi << std::dec << ", PS=\"" << params.ps << "\", RT=\""
              << params.rt << "\", pre-emph=" << formatNamedEnum(params.preEmph, PRE_EMPH_TABLE) << " us" << std::endl;

    FmRdsProcessor proc{{
        .audioSampleRate = audioRate,
        .channels        = info.channels,
        .mpxSampleRate   = MPX_SAMPLE_RATE,
        .peakDeviation   = PEAK_DEVIATION,
        .preEmphasisTau  = preEmphasisTauFor(params.preEmph),
    }};
    proc.encoder().setPi(params.pi);
    proc.encoder().setPs(params.ps);
    proc.encoder().setRt(params.rt);

    ngfmdmasync dma{params.freq, MPX_SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

    // Buffers sized per-block from the processor's runtime-derived block
    // size. std::vector here (rather than std::array) because the audio
    // sample rate is detected from the WAV header at run time, so the
    // M-aligned block size is not a compile-time constant.
    const int frames{proc.audioFramesPerBlock()};
    const int mpxLen{proc.mpxSamplesPerBlock()};
    std::vector<int16_t> pcmBuf(static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.channels));
    std::vector<float> audioBuf(static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.channels));
    std::vector<float> mpxBuf(static_cast<std::size_t>(mpxLen));

    while (running.load(std::memory_order_relaxed)) {
        const int n{readAudioBlock(info.channels, frames, pcmBuf, audioBuf)};
        if (n <= 0) {
            break;
        }
        proc.process({audioBuf.data(), audioBuf.size()}, {mpxBuf.data(), mpxBuf.size()});
        dma.SetFrequencySamples(mpxBuf.data(), mpxBuf.size());
    }

    dma.stop();
    std::cout << "pifmrds: transmission stopped." << std::endl;
    return 0;
}
