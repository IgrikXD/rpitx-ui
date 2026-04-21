/**
 * @file main.cpp
 * @brief Amplitude-modulation (AM) transmitter.
 *
 * Reads 16-bit PCM audio (48 kHz mono) from stdin, forms the canonical
 * DSB-FC AM envelope, and drives librpitx::amdmasync directly at the
 * requested carrier frequency. Transmission runs until SIGTERM / SIGINT
 * (the rpitx-ui launcher stops the process centrally via killall when
 * the user dismisses the dialog).
 *
 * @note Usage: piam <freq_Hz> [-h]
 *   - -h  Print the help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 20.04.2026
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

#include "am_processor.h"
#include "cli_utils.h"
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
     * amdmasync consumes one amplitude per sample at this rate; 48 kHz matches
     * the mandatory input PCM rate and is also the rate F5OEO's librpitx
     * testrpitx.cpp SimpleTestAm uses.
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
        std::cerr << "Usage: piam <freq_Hz>" << std::endl
                  << "  -h  Print this help message" << std::endl
                  << "  Reads 16-bit PCM mono audio at " << SAMPLE_RATE << " Hz from stdin." << std::endl;
    }

    /**
     * @brief AM parameters extracted from argv.
     */
    struct AmParameters {
        uint64_t freq{0};
    };

    /**
     * @brief Parse and validate command-line arguments.
     * @param argc Argument count.
     * @param argv Argument vector.
     * @param params Output - populated on success.
     * @return ParseResult indicating success, error, or a help request.
     */
    [[nodiscard]] ParseResult parseArgs(int argc, char* argv[], AmParameters& params) {
        // -h at any position short-circuits - regardless of positional-count state -
        // so that `piam -h`, `piam 100 -h`, etc. all print help and exit cleanly.
        if (containsFlag({argv + 1, argv + argc}, "-h")) {
            printUsage();
            return ParseResult::Help;
        }
        if (argc < 2) {
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

        return ParseResult::Ok;
    }

    /**
     * @brief Normalize a block of int16 PCM samples, run them through the AM
     *        processor, and hand the resulting envelope off to the DMA.
     *
     * amdmasync::SetAmSamples handles FIFO back-pressure internally (sleeps
     * until ~75 % of the FIFO is drained), so no explicit pacing is needed.
     *
     * @param am Active AM processor.
     * @param dma Active amdmasync instance.
     * @param scratch Scratch buffer (size BLOCK_SIZE) for the intermediate envelope; count must not exceed BLOCK_SIZE.
     * @param pcm PCM samples to process.
     * @param count Number of valid samples in pcm.
     */
    void processBlock(AmProcessor& am, amdmasync& dma, std::array<float, BLOCK_SIZE>& scratch, const int16_t* pcm,
                      int count) {
        // skipWavHeader returns count = 0 on a clean RIFF match (no carry-over
        // bytes to replay) - short-circuit so the DMA never sees a zero-length
        // batch, which is ill-defined for SetAmSamples.
        if (count <= 0) {
            return;
        }
        for (int i{0}; i < count; ++i) {
            scratch[i] = am.process(static_cast<float>(pcm[i]) / PCM16_MAX);
        }
        dma.SetAmSamples(scratch.data(), static_cast<size_t>(count));
    }

}  // namespace

int main(int argc, char* argv[]) {
    AmParameters params;
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
    // piam writes startup/shutdown status to stdout. If stdout is attached
    // to a pipe whose reader exits (e.g. `piam ... | tee log` where tee is
    // killed), those writes would raise SIGPIPE and abort us. Handle it as
    // a graceful shutdown instead.
    std::signal(SIGPIPE, handleSignal);

    std::cout << "piam: center=" << params.freq << " Hz, rate=" << SAMPLE_RATE << " Hz" << std::endl;

    AmProcessor am{static_cast<float>(SAMPLE_RATE)};
    amdmasync dma{params.freq, SAMPLE_RATE, DMA_BIT_DEPTH, DMA_FIFO_SIZE};

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
        processBlock(am, dma, outbuf, carry.samples.data(), carry.count);

        while (running.load(std::memory_order_relaxed)) {
            const auto n{static_cast<int>(std::fread(inbuf.data(), sizeof(int16_t), BLOCK_SIZE, stdin))};
            if (n <= 0) {
                break;
            }
            processBlock(am, dma, outbuf, inbuf.data(), n);
        }
    }

    dma.stop();
    std::cout << "piam: transmission stopped." << std::endl;
    return 0;
}
