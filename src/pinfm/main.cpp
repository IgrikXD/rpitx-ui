/**
 * @file main.cpp
 * @brief Narrow-band FM (NBFM) transmitter.
 *
 * Reads 16-bit PCM audio (48 kHz mono) from stdin, produces a per-sample
 * frequency-deviation stream (+-2.5 kHz narrow or +-5 kHz wide peak), and
 * drives librpitx::ngfmdmasync directly at the requested carrier frequency.
 * Replaces the legacy csdr-based testnfm.sh pipeline, which routed audio
 * through an IQ pipe and gave no bandwidth containment or level control.
 * Transmission runs until SIGTERM / SIGINT (the rpitx-ui launcher stops the
 * process centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pinfm <freq_Hz> [-m <mode>] [-h]
 *   - -m  NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)
 *   - -h  Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 24.04.2026
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

#include "cli_utils.h"
#include "nfm_processor.h"
#include "wav_utils.h"

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
     * @brief DMA sample rate in Hz (must match the input audio rate).
     *
     * ngfmdmasync consumes one frequency-deviation value per sample at this
     * rate; 48 kHz matches the mandatory input PCM rate and keeps the LPF
     * cutoff at 3 kHz well below Nyquist, so no resampling is needed and the
     * NBFM spectrum is not widened by aliasing of out-of-band audio energy.
     */
    constexpr uint32_t SAMPLE_RATE{48'000};

    /**
     * @brief Block size for PCM sample processing (~21 ms at 48 kHz).
     */
    constexpr int BLOCK_SIZE{1024};

    /**
     * @brief Normalization divisor for int16_t -> float [-1.0, 1.0] conversion (2^15).
     */
    constexpr float PCM16_MAX{static_cast<float>(std::numeric_limits<int16_t>::max()) + 1.0f};

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
                return 2'500.0f;
            case NfmMode::Wide:
                return 5'000.0f;
        }
        // Closed enum + -Wswitch guarantees this is unreachable; return 0 only
        // to silence "control reaches end of non-void function" on old toolchains.
        return 0.0f;
    }

    /**
     * @brief Atomic flag for graceful shutdown on signal reception.
     *
     * Must be lock-free to be safe to touch from a signal handler; statically
     * asserted below to fail fast on any exotic platform where it is not.
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
        std::cerr << "Usage: pinfm <freq_Hz> [options]" << std::endl
                  << "  -m <mode>  NBFM deviation mode: narrow (+-2.5 kHz) | wide (+-5 kHz, default)" << std::endl
                  << "  -h         Print this help message" << std::endl
                  << "  Reads 16-bit PCM mono audio at " << SAMPLE_RATE << " Hz from stdin." << std::endl;
    }

    /**
     * @brief NFM parameters extracted from argv.
     */
    struct NfmParameters {
        uint64_t freq{0};
        NfmMode mode{NfmMode::Wide};  ///< Default to wide (+-5 kHz, amateur VHF/UHF).
    };

    /**
     * @brief Parse and validate the single positional argument (frequency).
     * @param freqArg Frequency string (Hz, scientific notation allowed).
     * @param params Output - populated on success.
     * @return ParseResult::Ok on success, ::Error on error (message already
     *         printed to stderr).
     */
    [[nodiscard]] ParseResult parsePositionalArgs(std::string_view freqArg, NfmParameters& params) {
        // Frequency is parsed as double to accept scientific notation (e.g. "434e6")
        const auto freqOpt{parseNumericArg<double>(freqArg)};
        if (freqOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid frequency argument!" << std::endl;
            return ParseResult::Error;
        }
        // Guard the double -> uint64_t conversion. UINT64_MAX (2^64 - 1) is not
        // exactly representable as double, so casting it rounds up to 2^64 and
        // makes the comparison bound implementation-dependent. std::ldexp(1.0, 64)
        // is exactly 2^64 and is the strict upper bound: any finite double
        // strictly below it converts safely to uint64_t.
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
     *
     * @note -h is handled upstream in parseArgs() via a single pre-scan of argv,
     *       so this function only deals with value-carrying flags.
     *
     * @param args Slice of argv covering only the optional-flag region
     *             (i.e. the caller drops argv[0] and the frequency positional).
     * @param params Output - mutated in place with flag values.
     * @return ParseResult::Ok on success, ::Error on a bad flag (diagnostic
     *         already printed).
     */
    [[nodiscard]] ParseResult parseOptionalFlags(std::span<char* const> args, NfmParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            if (arg != "-m") {
                std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
                return ParseResult::Error;
            }

            if (++i >= args.size()) {
                std::cerr << "[ERROR] Option " << arg << " requires an argument!" << std::endl;
                return ParseResult::Error;
            }
            const std::string_view value{args[i]};

            const auto mode{parseNamedEnum(value, MODE_TABLE)};
            if (mode == std::nullopt) {
                std::cerr << "[ERROR] Unknown mode '" << value << "'. Expected narrow | wide." << std::endl;
                return ParseResult::Error;
            }
            params.mode = mode.value();
        }
        return ParseResult::Ok;
    }

    /**
     * @brief Parse and validate command-line arguments.
     * @param argc Argument count.
     * @param argv Argument vector.
     * @param params Output - populated on success.
     * @return ParseResult indicating success, error, or a help request.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], NfmParameters& params) {
        // -h at any position short-circuits - regardless of positional-count state -
        // so that `pinfm -h`, `pinfm 100 -h`, etc. all print help and exit cleanly.
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
        // Skip argv[0] (program name) and the frequency positional - pass only the flag tail.
        const std::span<char* const> flagArgs{argv + 2, argv + argc};
        if (const auto result{parseOptionalFlags(flagArgs, params)}; result != ParseResult::Ok) {
            return result;
        }
        return ParseResult::Ok;
    }

    /**
     * @brief Normalize a block of int16 PCM samples, run them through the NFM
     *        processor, and hand the resulting frequency deviations off to the DMA.
     *
     * ngfmdmasync::SetFrequencySamples handles FIFO back-pressure internally
     * (sleeps until ~75 % of the FIFO is drained), so no explicit pacing is needed.
     *
     * @param nfm Active NFM processor.
     * @param dma Active ngfmdmasync instance.
     * @param scratch Scratch buffer (size BLOCK_SIZE) for the intermediate deviations.
     * @param pcm PCM samples to process.
     * @param count Number of valid samples in pcm.
     */
    void processBlock(NfmProcessor& nfm, ngfmdmasync& dma, std::array<float, BLOCK_SIZE>& scratch, const int16_t* pcm,
                      int count) {
        // skipWavHeader returns count = 0 on a clean RIFF match (no carry-over
        // bytes to replay) - short-circuit so the DMA never sees a zero-length
        // batch, which is ill-defined for SetFrequencySamples.
        if (count <= 0) {
            return;
        }
        for (int i{0}; i < count; ++i) {
            scratch[i] = nfm.process(static_cast<float>(pcm[i]) / PCM16_MAX);
        }
        dma.SetFrequencySamples(scratch.data(), static_cast<size_t>(count));
    }

}  // namespace

int main(int argc, char* argv[]) {
    NfmParameters params;
    // No default branch: ParseResult is closed-set, so -Wswitch flags any future
    // enumerator that forgets to update this dispatch.
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

    const float peakDeviation{peakDeviationFor(params.mode)};
    std::cout << "pinfm: center=" << params.freq << " Hz, rate=" << SAMPLE_RATE
              << " Hz, mode=" << formatNamedEnum(params.mode, MODE_TABLE) << " (+-" << peakDeviation << " Hz)"
              << std::endl;

    NfmProcessor nfm{static_cast<float>(SAMPLE_RATE), peakDeviation};
    ngfmdmasync dma{params.freq, SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

    std::array<int16_t, BLOCK_SIZE> inbuf{};
    std::array<float, BLOCK_SIZE> outbuf{};

    // Skip the WAV header exactly once at stream start. The easytest.sh
    // pipeline (`while true; do cat $file; done`) holds the write end of
    // the pipe open across cat iterations, so fread never returns a
    // partial read at a file boundary - any follow-up RIFF headers land
    // mid-block and cannot be recovered from partial-read detection.
    const auto header{skipWavHeader()};
    if (header != std::nullopt) {
        const auto& carry{header.value()};
        processBlock(nfm, dma, outbuf, carry.samples.data(), carry.count);

        while (running.load(std::memory_order_relaxed)) {
            const auto n{static_cast<int>(std::fread(inbuf.data(), sizeof(int16_t), BLOCK_SIZE, stdin))};
            if (n <= 0) {
                break;
            }
            processBlock(nfm, dma, outbuf, inbuf.data(), n);
        }
    }

    dma.stop();
    std::cout << "pinfm: transmission stopped." << std::endl;
    return 0;
}
