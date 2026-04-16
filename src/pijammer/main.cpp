/**
 * @file main.cpp
 * @brief RF jammer transmitter for a user-defined bandwidth.
 *
 * Emits an RF disturbance centered on the requested carrier frequency,
 * spread across the specified bandwidth. The waveform can be uniform
 * pseudo-random noise, a fast sawtooth sweep, or random multi-tone hopping.
 * Transmission runs until SIGTERM / SIGINT (the rpitx-ui launcher stops
 * the process centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pijammer <freq_Hz> <bandwidth_Hz> [-m <mode>] [-t <tones>] [-s <rate>] [-h]
 *   - -m  Jamming mode: noise (default) | sweep | multitone
 *   - -t  Tone count for multitone mode (required for multitone)
 *   - -s  DMA sample rate in Hz (default 500000)
 *   - -h  Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.04.2026
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
#include <iostream>
#include <optional>
#include <span>
#include <string_view>

#include "cli_utils.h"
#include "jammer_processor.h"

namespace {
    /**
     * @brief DMA sample buffer depth.
     */
    constexpr int DMA_FIFO_SIZE{4096};

    /**
     * @brief DMA time-register precision in bits (matches other rpitx modules).
     */
    constexpr int DMA_BIT_DEPTH{14};

    /**
     * @brief Default DMA sample rate in Hz.
     */
    constexpr uint32_t DEFAULT_SAMPLE_RATE{500'000};

    /**
     * @brief Maximum allowed multitone tone count.
     *
     * Caps the pre-computed tone table size to guard against accidental or
     * malicious oversized allocations from CLI input.
     */
    constexpr int MAX_TONE_COUNT{1024};

    /**
     * @brief DMA drain fraction used to pace the refill loop.
     *
     * The loop sleeps for 3/4 of the time it takes to drain the FIFO at the
     * current sample rate, which keeps CPU usage low while ensuring the DMA
     * buffer never underruns.
     */
    constexpr float DMA_DRAIN_FRACTION{0.75F};

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
     * @brief Signal handler for SIGTERM and SIGINT.
     * @param sig Signal number.
     */
    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    /**
     * @brief Print the command-line usage to stderr.
     */
    void printUsage() {
        std::cerr << "Usage: pijammer <freq_Hz> <bandwidth_Hz> [options]" << std::endl
                  << "  -m <mode>     Jamming mode: noise (default) | sweep | multitone" << std::endl
                  << "  -t <count>    Tone count (required for multitone mode, [2, " << MAX_TONE_COUNT << "])"
                  << std::endl
                  << "  -s <rate_Hz>  DMA sample rate in Hz (default " << DEFAULT_SAMPLE_RATE << ")" << std::endl
                  << "  -h            Print this help message" << std::endl;
    }

    /**
     * @brief JammerMode textual names for CLI parsing and display.
     */
    constexpr std::array MODE_TABLE{
        NamedEnum<JammerMode>{"noise", JammerMode::Noise},
        NamedEnum<JammerMode>{"sweep", JammerMode::Sweep},
        NamedEnum<JammerMode>{"multitone", JammerMode::Multitone},
    };

    /**
     * @brief Jammer parameters extracted from argv.
     */
    struct JammerParameters {
        JammerMode mode{JammerMode::Noise};
        uint64_t freq{0};
        float bandwidth{0.0F};
        uint32_t sampleRate{DEFAULT_SAMPLE_RATE};
        std::optional<int> toneCount;  ///< Engaged iff -t was given on the CLI. Required for
                                       ///< Multitone mode; ignored (with a warning) otherwise.
    };

    /**
     * @brief Parse and validate the two positional arguments (frequency, bandwidth).
     * @param freqArg Frequency string (Hz, scientific notation allowed).
     * @param bwArg Bandwidth string (Hz).
     * @param params Output - populated on success.
     * @return ParseResult::Ok on success, ::Error on error (message already
     *         printed to stderr).
     */
    [[nodiscard]] ParseResult parsePositionalArgs(std::string_view freqArg, std::string_view bwArg,
                                                  JammerParameters& params) {
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

        const auto bandwidthOpt{parseNumericArg<float>(bwArg)};
        if (bandwidthOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid bandwidth argument!" << std::endl;
            return ParseResult::Error;
        }
        const float bandwidthValue{bandwidthOpt.value()};
        if (bandwidthValue <= 0.0F || std::isfinite(bandwidthValue) == false) {
            std::cerr << "[ERROR] Bandwidth must be a positive finite value!" << std::endl;
            return ParseResult::Error;
        }
        params.bandwidth = bandwidthValue;

        return ParseResult::Ok;
    }

    /**
     * @brief Walk the optional flag arguments and populate params.
     *
     * @note -h is handled upstream in parseArgs() via a single pre-scan of argv,
     *       so this function only deals with value-carrying flags.
     *
     * @param args   Slice of argv covering only the optional-flag region
     *               (i.e. the caller drops argv[0] and the two positional args).
     * @param params Output - mutated in place with flag values.
     * @return ParseResult::Ok on success, ::Error on a bad flag (diagnostic
     *         already printed).
     */
    [[nodiscard]] ParseResult parseOptionalFlags(std::span<char* const> args, JammerParameters& params) {
        for (std::size_t i{0}; i < args.size(); ++i) {
            const std::string_view arg{args[i]};

            // Reject unknown flags before consuming a value, so that a trailing unknown flag
            // (e.g. `pijammer 434e6 200000 -x`) surfaces as "Unknown option" instead of the
            // misleading "Option -x requires an argument".
            if (arg != "-m" && arg != "-t" && arg != "-s") {
                std::cerr << "[ERROR] Unknown option: " << arg << std::endl;
                return ParseResult::Error;
            }

            // All known flags here take a value argument - advance and bounds-check.
            if (++i >= args.size()) {
                std::cerr << "[ERROR] Option " << arg << " requires an argument!" << std::endl;
                return ParseResult::Error;
            }
            const std::string_view value{args[i]};

            if (arg == "-m") {
                const auto mode{parseNamedEnum(value, MODE_TABLE)};
                if (mode == std::nullopt) {
                    std::cerr << "[ERROR] Unknown mode '" << value << "'. Expected noise | sweep | multitone."
                              << std::endl;
                    return ParseResult::Error;
                }
                params.mode = mode.value();
            } else if (arg == "-t") {
                if (const auto result{assignNumericFlag(value, "tone count", params.toneCount)};
                    result != ParseResult::Ok) {
                    return result;
                }
            } else if (arg == "-s") {
                if (const auto result{assignNumericFlag(value, "sample rate", params.sampleRate)};
                    result != ParseResult::Ok) {
                    return result;
                }
            }
        }
        return ParseResult::Ok;
    }

    /**
     * @brief Run cross-field validation after all CLI values have been collected.
     * @param params Populated options struct.
     * @return ParseResult::Ok on success, ::Error if any invariant is violated.
     */
    [[nodiscard]] ParseResult validateOptions(const JammerParameters& params) {
        // Warn (but don't fail) when -t is passed without -m multitone: the value
        // has no effect in noise/sweep modes and would otherwise be silently dropped,
        // which is confusing when the user has explicitly supplied it.
        if (params.mode != JammerMode::Multitone && params.toneCount != std::nullopt) {
            std::cerr << "[WARN] -t is only meaningful with -m multitone, ignoring." << std::endl;
        }

        // Nyquist: peak frequency deviation is bandwidth/2, which must fit strictly
        // within sampleRate/2. Equivalently, bandwidth must stay below sampleRate;
        // equality sits exactly on the aliasing boundary and is disallowed. Note
        // that sampleRate == 0 is also caught here, since bandwidth is already
        // validated to be strictly positive upstream.
        //
        // sampleRate is cast to double so a uint32_t above ~16.7 MHz is not
        // rounded into a 24-bit float mantissa (which would shift the Nyquist
        // boundary by up to ~256 Hz). The float bandwidth is then implicitly
        // widened to double by the usual arithmetic conversions; double's
        // 53-bit mantissa represents both sides exactly.
        if (params.bandwidth >= static_cast<double>(params.sampleRate)) {
            std::cerr << "[ERROR] Bandwidth (" << params.bandwidth << " Hz) must be below sample rate ("
                      << params.sampleRate << " Hz) - Nyquist limit!" << std::endl;
            return ParseResult::Error;
        }

        // Multitone requires an explicit -t value in [2, MAX_TONE_COUNT]. There is
        // no sensible default (any fixed number would be arbitrary), and a single
        // tone degenerates to a plain carrier. Short-circuit evaluation guarantees
        // .value() is only reached when the optional is engaged.
        if (params.mode == JammerMode::Multitone && (params.toneCount == std::nullopt || params.toneCount.value() < 2 ||
                                                     params.toneCount.value() > MAX_TONE_COUNT)) {
            std::cerr << "[ERROR] Multitone mode requires -t <count> in [2, " << MAX_TONE_COUNT << "]!" << std::endl;
            return ParseResult::Error;
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
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], JammerParameters& params) {
        // -h at any position short-circuits - regardless of positional-count state -
        // so that `pijammer -h`, `pijammer 100 -h`, etc. all print help and exit cleanly.
        if (containsFlag({argv + 1, argv + argc}, "-h")) {
            printUsage();
            return ParseResult::Help;
        }
        if (argc < 3) {
            printUsage();
            return ParseResult::Error;
        }
        if (const auto result{parsePositionalArgs(argv[1], argv[2], params)}; result != ParseResult::Ok) {
            return result;
        }
        // Skip argv[0] (program name) and the two positional args - pass only the flag tail.
        const std::span<char* const> flagArgs{argv + 3, argv + argc};
        if (const auto result{parseOptionalFlags(flagArgs, params)}; result != ParseResult::Ok) {
            return result;
        }
        if (const auto result{validateOptions(params)}; result != ParseResult::Ok) {
            return result;
        }
        return ParseResult::Ok;
    }

}  // namespace

int main(int argc, char* argv[]) {
    JammerParameters params;
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

    std::cout << "pijammer: center=" << params.freq << " Hz, bandwidth=" << params.bandwidth
              << " Hz, mode=" << formatNamedEnum(params.mode, MODE_TABLE) << ", rate=" << params.sampleRate << " Hz";
    if (params.mode == JammerMode::Multitone) {
        std::cout << ", tones=" << params.toneCount.value();
    }
    std::cout << std::endl;

    JammerProcessor jammer{{
        .mode       = params.mode,
        .bandwidth  = params.bandwidth,
        .sampleRate = params.sampleRate,
        // JammerConfig::toneCount is ignored outside Multitone (see jammer_processor.h),
        // so the 0 fallback is inert there; Multitone guarantees the optional is engaged.
        .toneCount = params.toneCount.value_or(0),
    }};

    ngfmdmasync dma{params.freq, params.sampleRate, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

    // Sleep pattern borrowed from pichirp: wake every 3/4 FIFO drain period.
    const auto sleepUs{static_cast<useconds_t>(DMA_FIFO_SIZE * 1'000'000.0F * DMA_DRAIN_FRACTION /
                                               static_cast<float>(params.sampleRate))};

    while (running.load(std::memory_order_relaxed)) {
        usleep(sleepUs);

        if (const int available{dma.GetBufferAvailable()}; available > DMA_FIFO_SIZE / 2) {
            const int index{dma.GetUserMemIndex()};
            for (int j{0}; j < available; ++j) {
                dma.SetFrequencySample(index + j, jammer.nextSample());
            }
        }
    }

    dma.stop();
    std::cout << "pijammer: transmission stopped." << std::endl;
    return 0;
}
