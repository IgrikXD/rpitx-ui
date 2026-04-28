/**
 * @file pirfgen.cpp
 * @brief Wideband RF generator transmitter implementation for a user-defined bandwidth.
 *
 * Emits an RF waveform centered on the requested carrier frequency,
 * spread across the specified bandwidth. The waveform can be uniform
 * pseudo-random noise, a fast sawtooth sweep, or random multi-tone hopping.
 * Transmission runs until SIGTERM / SIGINT (the rpitx-ui launcher stops
 * the process centrally via killall when the user dismisses the dialog).
 *
 * @note Usage: pirfgen --freq <Hz> --bandwidth <Hz> [--sample-rate <Hz>]
 *               [--mode noise|sweep|multitone] [--tone-count <count>] [-h | --help]
 *   - --freq         Carrier frequency in Hz
 *   - --bandwidth    RF bandwidth in Hz (must be below --sample-rate)
 *   - --sample-rate  DMA sample rate in Hz (default 500000)
 *   - --mode         RF generator mode: noise (default) | sweep | multitone
 *   - --tone-count   Number of equidistant tones (only valid with --mode multitone)
 *   - -h, --help     Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pirfgen.h"

#include <CLI/CLI.hpp>
#include <librpitx/librpitx.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <string>

#include "cli_common.h"
#include "cli_validators.h"

namespace pirfgen {
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

    const char* modeName(RfGenMode mode) {
        switch (mode) {
            case RfGenMode::Noise:
                return "noise";
            case RfGenMode::Sweep:
                return "sweep";
            case RfGenMode::Multitone:
                return "multitone";
        }
        return "unknown";
    }

    void handleSignal([[maybe_unused]] int sig) {
        running.store(false, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], RfGenParameters& params) {
        CLI::App app{"Wideband RF generator (noise / sweep / multitone)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--bandwidth", params.bandwidth, "RF bandwidth in Hz (must be below --sample-rate)")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat);
        app.add_option("--sample-rate", params.sampleRate, "DMA sample rate in Hz (default 500000)")
            ->check(CLI::PositiveNumber);

        const std::map<std::string, RfGenMode> modeMap{
            {"noise", RfGenMode::Noise},
            {"sweep", RfGenMode::Sweep},
            {"multitone", RfGenMode::Multitone},
        };
        app.add_option("--mode", params.mode, "RF generator mode: noise (default) | sweep | multitone")
            ->transform(CLI::CheckedTransformer(modeMap, CLI::ignore_case));

        // --tone-count is captured into a local optional so that "user did not
        // supply this option" stays distinguishable from "user supplied 0".
        // Only copied into params.toneCount after semantic validation succeeds,
        // so RfGenConfig never sees a stale value when a non-multitone mode
        // was paired with --tone-count.
        std::optional<int> toneCountOpt;
        app.add_option("--tone-count", toneCountOpt,
                       "Number of equidistant tones for --mode multitone (range [2, 1024])")
            ->check(CLI::Range(2, MAX_TONE_COUNT));

        if (const auto result{rpitx::cli::finalizeParse(app, argc, argv)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (const auto result{rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // Nyquist: peak frequency deviation is bandwidth/2, which must fit strictly
        // within sampleRate/2. Equivalently, bandwidth must stay below sampleRate;
        // equality sits exactly on the aliasing boundary and is disallowed.
        //
        // sampleRate is cast to double so a uint32_t above ~16.7 MHz is not
        // rounded into a 24-bit float mantissa (which would shift the Nyquist
        // boundary by up to ~256 Hz). The float bandwidth is then implicitly
        // widened to double by the usual arithmetic conversions; double's
        // 53-bit mantissa represents both sides exactly.
        if (params.bandwidth >= static_cast<double>(params.sampleRate)) {
            std::cerr << "[ERROR] --bandwidth (" << params.bandwidth << " Hz) must be below --sample-rate ("
                      << params.sampleRate << " Hz) - Nyquist limit!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        // --tone-count is meaningful only with --mode multitone. Reject (not warn)
        // when paired with another mode so the user does not silently transmit a
        // different waveform than they asked for.
        if (params.mode != RfGenMode::Multitone && toneCountOpt != std::nullopt) {
            std::cerr << "[ERROR] --tone-count is only valid with --mode multitone!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        // Multitone requires an explicit --tone-count value; there is no sensible
        // default (any fixed number would be arbitrary), and a single tone
        // degenerates to a plain carrier.
        if (params.mode == RfGenMode::Multitone && toneCountOpt == std::nullopt) {
            std::cerr << "[ERROR] --mode multitone requires --tone-count <count> in [2, " << MAX_TONE_COUNT << "]!"
                      << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.toneCount = toneCountOpt;
        return rpitx::cli::ParseResult::Ok;
    }

    int run(int argc, char* argv[]) {
        RfGenParameters params;
        // No default branch: ParseResult is closed-set, so -Wswitch flags any future
        // enumerator that forgets to update this dispatch.
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

        std::cout << "pirfgen: center=" << params.transmissionFrequency << " Hz, bandwidth=" << params.bandwidth
                  << " Hz, mode=" << modeName(params.mode) << ", rate=" << params.sampleRate << " Hz";
        if (params.mode == RfGenMode::Multitone) {
            std::cout << ", tones=" << params.toneCount.value();
        }
        std::cout << std::endl;

        RfGenProcessor rfgen{{
            .mode       = params.mode,
            .bandwidth  = params.bandwidth,
            .sampleRate = params.sampleRate,
            // RfGenConfig::toneCount is ignored outside Multitone (see rfgen_processor.h),
            // so the 0 fallback is inert there; Multitone guarantees the optional is engaged.
            .toneCount = params.toneCount.value_or(0),
        }};

        ngfmdmasync dma{params.transmissionFrequency, params.sampleRate, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

        // Sleep pattern borrowed from pichirp: wake every 3/4 FIFO drain period.
        const auto sleepUs{static_cast<useconds_t>(DMA_FIFO_SIZE * 1'000'000.0F * DMA_DRAIN_FRACTION /
                                                   static_cast<float>(params.sampleRate))};

        while (running.load(std::memory_order_relaxed)) {
            usleep(sleepUs);

            if (const int available{dma.GetBufferAvailable()}; available > DMA_FIFO_SIZE / 2) {
                const int index{dma.GetUserMemIndex()};
                for (int j{0}; j < available; ++j) {
                    dma.SetFrequencySample(index + j, rfgen.nextSample());
                }
            }
        }

        dma.stop();
        std::cout << "pirfgen: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pirfgen

int main(int argc, char* argv[]) {
    return pirfgen::run(argc, argv);
}
