/**
 * @file main.cpp
 * @brief Sinusoidal FM chirp transmitter.
 *
 * Emits a sinusoidal frequency sweep centered on the requested carrier,
 * with a peak deviation of bandwidth / 2 and one full cycle every
 * sweep_time seconds. Transmission runs until SIGTERM / SIGINT (the
 * rpitx-ui launcher stops the process centrally via killall when the
 * user dismisses the dialog).
 *
 * @note Usage: pichirp <freq_Hz> <bandwidth_Hz> <sweep_time_s> [-h]
 *   - -h  Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 17.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <librpitx/librpitx.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>

#include "cli_utils.h"

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
     * @brief DMA sample rate in Hz.
     */
    constexpr uint32_t SAMPLE_RATE{200'000};

    /**
     * @brief DMA drain fraction used to pace the refill loop.
     *
     * The loop sleeps for 3/4 of the time it takes to drain the FIFO at the
     * current sample rate, which keeps CPU usage low while ensuring the DMA
     * buffer never underruns.
     */
    constexpr float DMA_DRAIN_FRACTION{0.75F};

    /**
     * @brief Minimum number of samples per sweep cycle.
     *
     * Below this floor the sine is sampled too coarsely to produce a
     * meaningful chirp (a 1-sample period degenerates to a flat carrier).
     */
    constexpr int MIN_PERIOD_SAMPLES{100};

    /**
     * @brief Maximum number of samples per sweep cycle.
     *
     * Keeps (sweep_time * sample_rate) comfortably inside int range and
     * preserves double-precision phase accuracy throughout one cycle.
     */
    constexpr int MAX_PERIOD_SAMPLES{1'000'000'000};

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
        std::cerr << "Usage: pichirp <freq_Hz> <bandwidth_Hz> <sweep_time_s>" << std::endl
                  << "  -h  Print this help message" << std::endl;
    }

    /**
     * @brief Chirp parameters extracted from argv.
     */
    struct ChirpParameters {
        uint64_t freq{0};
        float bandwidth{0.0F};
        float sweepTime{0.0F};
        int periodSamples{0};  ///< Derived: sweepTime * SAMPLE_RATE, validated in parseArgs.
    };

    /**
     * @brief Parse and validate command-line arguments.
     * @param argc Argument count.
     * @param argv Argument vector.
     * @param params Output - populated on success.
     * @return ParseResult indicating success, error, or a help request.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], ChirpParameters& params) {
        // -h at any position short-circuits - regardless of positional-count state -
        // so that `pichirp -h`, `pichirp 100 -h`, etc. all print help and exit cleanly.
        if (containsFlag({argv + 1, argv + argc}, "-h")) {
            printUsage();
            return ParseResult::Help;
        }
        if (argc < 4) {
            printUsage();
            return ParseResult::Error;
        }

        // Frequency is parsed as double to accept scientific notation (e.g. "434e6")
        const auto freqOpt{parseNumericArg<double>(argv[1])};
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

        const auto bandwidthOpt{parseNumericArg<float>(argv[2])};
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

        const auto sweepTimeOpt{parseNumericArg<float>(argv[3])};
        if (sweepTimeOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid sweep time argument!" << std::endl;
            return ParseResult::Error;
        }
        const float sweepTimeValue{sweepTimeOpt.value()};
        if (sweepTimeValue <= 0.0F || std::isfinite(sweepTimeValue) == false) {
            std::cerr << "[ERROR] Sweep time must be a positive finite value!" << std::endl;
            return ParseResult::Error;
        }
        // Compute the sweep period in double to avoid float overflow / precision loss
        // for large sweep_time values, and reject anything outside the supported range.
        // This guards the int cast against UB and rules out degenerate period-of-one-sample
        // inputs that would produce a silent carrier.
        const double samplesPerCycle{static_cast<double>(sweepTimeValue) * static_cast<double>(SAMPLE_RATE)};
        if (samplesPerCycle < static_cast<double>(MIN_PERIOD_SAMPLES) ||
            samplesPerCycle > static_cast<double>(MAX_PERIOD_SAMPLES)) {
            std::cerr << "[ERROR] Sweep time (" << sweepTimeValue << " s) must yield [" << MIN_PERIOD_SAMPLES << ", "
                      << MAX_PERIOD_SAMPLES << "] samples at " << SAMPLE_RATE << " Hz!" << std::endl;
            return ParseResult::Error;
        }
        params.sweepTime     = sweepTimeValue;
        params.periodSamples = static_cast<int>(samplesPerCycle);

        // Nyquist: peak frequency deviation is bandwidth/2, which must fit strictly
        // within sampleRate/2. Equivalently, bandwidth must stay below sampleRate;
        // equality sits exactly on the aliasing boundary and is disallowed.
        if (params.bandwidth >= static_cast<double>(SAMPLE_RATE)) {
            std::cerr << "[ERROR] Bandwidth (" << params.bandwidth << " Hz) must be below sample rate (" << SAMPLE_RATE
                      << " Hz) - Nyquist limit!" << std::endl;
            return ParseResult::Error;
        }

        return ParseResult::Ok;
    }

}  // namespace

int main(int argc, char* argv[]) {
    ChirpParameters params;
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

    std::cout << "pichirp: center=" << params.freq << " Hz, bandwidth=" << params.bandwidth
              << " Hz, sweep_time=" << params.sweepTime << " s" << std::endl;

    ngfmdmasync dma{params.freq, SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

    // Peak frequency deviation in Hz (half the requested bandwidth, symmetric around carrier).
    const float deviation{params.bandwidth * 0.5F};
    // Phase math stays in double so that count -> angle retains full precision even for
    // long sweeps (float loses integer precision above 2^24 ~= 16.8 M).
    const double phaseStep{2.0 * std::numbers::pi_v<double> / static_cast<double>(params.periodSamples)};

    const auto sleepUs{
        static_cast<useconds_t>(DMA_FIFO_SIZE * 1'000'000.0F * DMA_DRAIN_FRACTION / static_cast<float>(SAMPLE_RATE))};

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
