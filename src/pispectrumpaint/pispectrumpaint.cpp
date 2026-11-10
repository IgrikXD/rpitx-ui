/**
 * @file pispectrumpaint.cpp
 * @brief Spectrum painting transmitter implementation.
 *
 * Paints a grayscale picture onto the waterfall of a receiver: every row of
 * the picture is transmitted as a sweep across the requested bandwidth, one
 * pixel per DMA sample, with the pixel brightness selecting the carrier
 * amplitude. The picture is read as a binary PGM (P5) image from a file or
 * from stdin and drives librpitx::iqdmasync in MODE_FREQ_A directly.
 * Transmission ends with the picture (or runs forever when --loop is set), or
 * on SIGTERM / SIGINT.
 *
 * @note Usage: pispectrumpaint --freq <Hz> --bandwidth <Hz> [--sample-rate <Hz>] (--image <path> | --stdin)
 *               [--loop] [-h | --help]
 *   - --freq         Carrier frequency in Hz
 *   - --bandwidth    Painted RF bandwidth in Hz
 *   - --sample-rate  DMA sample rate in Hz, one pixel per sample (default 10000)
 *   - --image        Path to the picture (binary PGM, P5)
 *   - --stdin        Read the picture from stdin (pipe-friendly; --loop unsupported)
 *   - --loop         Repaint the picture on EOF
 *   - -h, --help     Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pispectrumpaint.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <atomic>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "pgm_reader.h"
#include "realtime_scheduling.h"
#include "spectrum_painter.h"

namespace pispectrumpaint {
    namespace {
        /**
         * @brief Atomic shutdown flag accessed from a signal handler.
         *
         * Uses int (not bool) because std::atomic<bool> is not always lock-free
         * on 32-bit ARMv6 (default Raspberry Pi OS 32-bit target). The
         * static_assert below fails fast on any platform where the chosen type
         * is not lock-free.
         */
        std::atomic<int> running{1};
        static_assert(std::atomic<int>::is_always_lock_free,
                      "std::atomic<int> must be lock-free for signal-handler access");

        /**
         * @brief Duration of one picture row in milliseconds.
         */
        [[nodiscard]] double lineTimeMs(int width, int sampleRate) {
            return static_cast<double>(width) * 1000.0 / static_cast<double>(sampleRate);
        }

    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], SpectrumPaintParameters& params) {
        CLI::App app{"Spectrum painting transmitter (PGM picture -> librpitx iqdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option(
               "--bandwidth", params.bandwidth, "Painted RF bandwidth in Hz (the picture width is spread over it)")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat);
        app.add_option("--sample-rate",
                       params.sampleRate,
                       "DMA sample rate in Hz, one picture pixel per sample (default " +
                           std::to_string(DEFAULT_SAMPLE_RATE) + ")")
            ->check(CLI::Range(MIN_SAMPLE_RATE, MAX_SAMPLE_RATE));
        // --image and --stdin are mutually exclusive and exactly one is
        // required: an option group with require_option(1) lets CLI11 enforce
        // both invariants in one place and surfaces a clean diagnostic in
        // --help, instead of a manual post-parse check that would silently
        // accept neither or both.
        auto* inputGroup{app.add_option_group("input", "Picture input source (exactly one is required)")};
        inputGroup->add_option("--image", params.imagePath, "Input picture path (binary PGM, P5)");
        inputGroup->add_flag(
            "--stdin", params.useStdin, "Read the picture from stdin (pipe-friendly; --loop unsupported)");
        inputGroup->require_option(1);
        app.add_flag("--loop", params.loop, "Repaint the picture on EOF (requires --image)");

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // --loop rewinds the input at EOF, which stdin cannot do.
        if (params.useStdin && params.loop) {
            std::cerr << "[ERROR] --loop is unsupported with --stdin." << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        if (params.bandwidth > static_cast<float>(MAX_BANDWIDTH)) {
            std::cerr << "[ERROR] --bandwidth (" << params.bandwidth << " Hz) must not exceed " << MAX_BANDWIDTH
                      << " Hz!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        if (const auto result{rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // The painted band spans --freq +- bandwidth / 2 and has to stay above 0 Hz.
        if (params.bandwidth / 2.0 >= static_cast<double>(params.transmissionFrequency)) {
            std::cerr << "[ERROR] --bandwidth (" << params.bandwidth << " Hz) must be below twice --freq ("
                      << params.transmissionFrequency << " Hz) so the painted band stays above 0 Hz!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        return rpitx::cli::ParseResult::Ok;
    }

    int run(int argc, char* argv[]) {
        SpectrumPaintParameters params;
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
        // SIGPIPE: keep shutdown behavior explicit when launched from shell wrappers.
        std::signal(SIGPIPE, handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

        auto reader{makePgmReader(params.useStdin, params.imagePath)};
        if (reader == nullptr) {
            return 1;
        }
        if (params.loop && reader->seekable() == false) {
            std::cerr << "[ERROR] Image source is not seekable; Loop is unsupported on this input." << std::endl;
            return 1;
        }

        // The first header is read before the DMA is set up, so a bad picture never reaches the hardware.
        switch (reader->readHeader()) {
            case PgmReadStatus::Ok:
                break;
            case PgmReadStatus::End:
                std::cerr << "[ERROR] pispectrumpaint: no PGM image found in the input." << std::endl;
                return 1;
            case PgmReadStatus::Error:
                std::cerr << "[ERROR] pispectrumpaint: " << reader->lastError() << std::endl;
                return 1;
        }

        PgmHeader announcedHeader{reader->header()};
        std::cout << "pispectrumpaint: center=" << params.transmissionFrequency << " Hz, bandwidth=" << params.bandwidth
                  << " Hz, rate=" << params.sampleRate << " Hz, format=" << reader->description()
                  << ", line_time=" << lineTimeMs(announcedHeader.width, params.sampleRate)
                  << " ms, loop=" << (params.loop ? "yes" : "no") << std::endl;

        // Wrap DMA construction and the painting loop in a try / catch:
        // iqdmasync surfaces DMA setup failures as exceptions and paintRow /
        // PgmReader throw on contract violations. A stray throw escaping main
        // would terminate via std::terminate without flushing the diagnostic.
        try {
            iqdmasync dma{params.transmissionFrequency,
                          static_cast<uint32_t>(params.sampleRate),
                          DMA_CHANNEL,
                          DMA_FIFO_SIZE,
                          MODE_FREQ_A};
            // The constructor switched the carrier on; keep it off while the PLL setup is checked.
            dma.disableclk(CARRIER_GPIO);

            // The constructor chose the PLL divider for the sample rate; the painted band is far wider.
            // clkgpio::SetCenterFrequency runs ComputeBestLO with a half-span guard band on the PLL
            // frequency, which is twice the carrier on BCM2711 (fixed post-divider), so the same
            // arguments are prepared here to check the divider before the PLL is programmed.
            const uint64_t pllFrequencyFactor{dma.clkgpio::pi_is_2711 ? 2U : 1U};
            const double halfBandwidth{params.bandwidth / 2.0};
            const int guardBand{static_cast<int>(std::ceil(halfBandwidth * static_cast<double>(pllFrequencyFactor)))};
            const uint64_t pllFrequency{params.transmissionFrequency * pllFrequencyFactor};

            // ComputeBestLO only selects the divider; it fails outside the carrier range of the PLL (and keeps
            // the previous divider), and a wide band at a low carrier can only be swept with a small divider,
            // whose PLL rate lies below the minimum - either setting freezes the board, so refuse before
            // programming it.
            if (dma.ComputeBestLO(pllFrequency, guardBand) != 0) {
                std::cerr << "[ERROR] pispectrumpaint: no PLL setting generates a " << params.transmissionFrequency
                          << " Hz carrier on this board." << std::endl;
                return 1;
            }
            const double pllRate{static_cast<double>(pllFrequency) * dma.PllFixDivider};
            if (dma.PllFixDivider > 1 && pllRate < MIN_PLL_RATE_HZ) {
                std::cerr << "[ERROR] pispectrumpaint: no PLL setting sweeps a " << params.bandwidth << " Hz band at "
                          << params.transmissionFrequency << " Hz (the PLL would run at " << pllRate / 1e6
                          << " MHz, below its " << MIN_PLL_RATE_HZ / 1e6
                          << " MHz minimum); use a higher --freq or a smaller --bandwidth." << std::endl;
                return 1;
            }
            dma.SetCenterFrequency(params.transmissionFrequency, guardBand);

            // The DMA writes only the 20-bit fractional part of the PLL multiplier: when no divider keeps the
            // band between two integer steps, the fraction wraps across the band and so would the picture.
            if (dma.GetMasterFrac(-halfBandwidth) > dma.GetMasterFrac(halfBandwidth)) {
                std::cerr << "[ERROR] pispectrumpaint: the painted band crosses an integer step of the PLL "
                             "multiplier at this carrier, part of the picture would wrap by a whole step; use a "
                             "smaller --bandwidth or another --freq."
                          << std::endl;
                return 1;
            }

            // Slot 0 and the start-up burst of the engine get carrier-off samples, so the first pixel reaches the air.
            startTransmission(dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC);

            std::vector<std::uint8_t> row;
            std::vector<std::complex<float>> rowSamples;
            auto status{PgmReadStatus::Ok};

            while (running.load(std::memory_order_relaxed)) {
                if (status == PgmReadStatus::End) {
                    if (params.loop == false) {
                        break;
                    }
                    if (reader->rewind() == false) {
                        std::cerr << "[ERROR] pispectrumpaint: " << reader->lastError() << std::endl;
                        dma.stop();
                        return 1;
                    }
                    status = reader->readHeader();
                    if (status == PgmReadStatus::End) {
                        std::cerr << "[ERROR] pispectrumpaint: no PGM image found after rewinding the input."
                                  << std::endl;
                        dma.stop();
                        return 1;
                    }
                    continue;
                }
                if (status == PgmReadStatus::Error) {
                    std::cerr << "[ERROR] pispectrumpaint: " << reader->lastError() << std::endl;
                    dma.stop();
                    return 1;
                }

                const PgmHeader& header{reader->header()};
                if (header.width != announcedHeader.width || header.height != announcedHeader.height) {
                    announcedHeader = header;
                    std::cout << "pispectrumpaint: format=" << reader->description()
                              << ", line_time=" << lineTimeMs(header.width, params.sampleRate) << " ms" << std::endl;
                }
                row.resize(static_cast<std::size_t>(header.width));
                rowSamples.resize(static_cast<std::size_t>(header.width));

                // SetIQSamples blocks until the row is in the DMA FIFO (see
                // iqdmasync.cpp upstream), so no manual pacing is needed here.
                while (reader->rowsRemaining() > 0 && running.load(std::memory_order_relaxed)) {
                    if (reader->readRow(row) != PgmReadStatus::Ok) {
                        std::cerr << "[ERROR] pispectrumpaint: " << reader->lastError() << std::endl;
                        dma.stop();
                        return 1;
                    }
                    paintRow(row, params.bandwidth, rowSamples);
                    dma.SetIQSamples(rowSamples.data(), rowSamples.size(), DEFAULT_HARMONIC);
                }
                // Stop before the next header: skipping the rest of a picture could block on stdin.
                if (running.load(std::memory_order_relaxed) == false) {
                    break;
                }
                status = reader->readHeader();
            }

            // The queued rows are transmitted when the picture ended by itself, discarded on a stop request.
            const bool inputEnded{running.load(std::memory_order_relaxed) != 0};
            stopTransmission(dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC, inputEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pispectrumpaint: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pispectrumpaint: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pispectrumpaint

int main(int argc, char* argv[]) {
    return pispectrumpaint::run(argc, argv);
}
