/**
 * @file pichirp.cpp
 * @brief Sinusoidal FM chirp transmitter implementation.
 *
 * Emits a sinusoidal frequency sweep centered on the requested carrier,
 * with a peak deviation of bandwidth / 2 and one full cycle every
 * sweep_time seconds. Transmission runs until SIGTERM / SIGINT (the
 * rpitx-ui launcher stops the process centrally via killall when the
 * user dismisses the dialog).
 *
 * @note Usage: pichirp --freq <Hz> --bandwidth <Hz> --sweep-time <seconds> [-h | --help]
 *   - --freq         Carrier frequency in Hz
 *   - --bandwidth    RF bandwidth in Hz (peak deviation = bandwidth / 2)
 *   - --sweep-time   Sweep time in seconds
 *   - -h, --help     Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pichirp.h"

#include <librpitx/librpitx.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <atomic>
#include <cmath>
#include <csignal>
#include <iostream>
#include <numbers>
#include <string>

#include "cli_common.h"
#include "cli_validators.h"

namespace pichirp {
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

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], ChirpParameters& params) {
        CLI::App app{"Sinusoidal FM chirp transmitter"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--bandwidth", params.bandwidth, "RF bandwidth in Hz (peak deviation = bandwidth / 2)")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat);
        app.add_option("--sweep-time", params.sweepTime, "Sweep time in seconds")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat);

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (const auto result{rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // Compute the sweep period in double to avoid float overflow / precision loss
        // for large sweep_time values, and reject anything outside the supported range.
        // This guards the int cast against UB and rules out degenerate period-of-one-sample
        // inputs that would produce a silent carrier.
        const double samplesPerCycle{static_cast<double>(params.sweepTime) * static_cast<double>(SAMPLE_RATE)};
        if (samplesPerCycle < static_cast<double>(MIN_PERIOD_SAMPLES) ||
            samplesPerCycle > static_cast<double>(MAX_PERIOD_SAMPLES)) {
            std::cerr << "[ERROR] --sweep-time (" << params.sweepTime << " s) must yield [" << MIN_PERIOD_SAMPLES
                      << ", " << MAX_PERIOD_SAMPLES << "] samples at " << SAMPLE_RATE << " Hz!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        params.periodSamples = static_cast<int>(samplesPerCycle);

        // Nyquist: peak frequency deviation is bandwidth/2, which must fit strictly
        // within sampleRate/2. Equivalently, bandwidth must stay below sampleRate;
        // equality sits exactly on the aliasing boundary and is disallowed.
        if (params.bandwidth >= static_cast<double>(SAMPLE_RATE)) {
            std::cerr << "[ERROR] --bandwidth (" << params.bandwidth << " Hz) must be below sample rate ("
                      << SAMPLE_RATE << " Hz) - Nyquist limit!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        return rpitx::cli::ParseResult::Ok;
    }

    int run(int argc, char* argv[]) {
        ChirpParameters params;
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

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);

        std::cout << "pichirp: center=" << params.transmissionFrequency << " Hz, bandwidth=" << params.bandwidth
                  << " Hz, sweep_time=" << params.sweepTime << " s" << std::endl;

        ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

        // Peak frequency deviation in Hz (half the requested bandwidth, symmetric around carrier).
        const float deviation{params.bandwidth * 0.5F};
        // Phase math stays in double so that count -> angle retains full precision even for
        // long sweeps (float loses integer precision above 2^24 ~= 16.8 M).
        const double phaseStep{2.0 * std::numbers::pi_v<double> / static_cast<double>(params.periodSamples)};

        const auto sleepUs{static_cast<useconds_t>(DMA_FIFO_SIZE * 1'000'000.0F * DMA_DRAIN_FRACTION /
                                                   static_cast<float>(SAMPLE_RATE))};

        int count{0};
        while (running.load(std::memory_order_relaxed)) {
            usleep(sleepUs);

            if (const int available{dma.GetBufferAvailable()}; available > DMA_FIFO_SIZE / 2) {
                const int index{dma.GetUserMemIndex()};
                for (int j{0}; j < available; ++j) {
                    dma.SetFrequencySample(index + j, static_cast<float>(deviation * std::sin(phaseStep * count)));
                    if (++count >= params.periodSamples) {
                        count = 0;
                    }
                }
            }
        }

        dma.stop();
        std::cout << "pichirp: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pichirp

int main(int argc, char* argv[]) {
    return pichirp::run(argc, argv);
}
