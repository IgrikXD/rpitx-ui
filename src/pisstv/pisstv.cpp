/**
 * @file pisstv.cpp
 * @brief Slow-scan television (SSTV) transmitter implementation.
 *
 * Transmits a color picture in one of the Martin M1 / M2, Scottie S1 / S2,
 * Robot 36 / 72 and PD-90 / 120 / 180 modes. The picture is read as a binary
 * PPM (P6) image of the picture size of the mode from a file or from stdin.
 * SSTV is a sequence of audio tones sent over single sideband, so every tone
 * is transmitted as the carrier shifted above the requested frequency by the
 * frequency of the tone: the tones drive librpitx::ngfmdmasync directly, and
 * a receiver tuned to the requested frequency in USB mode gets them back.
 * Transmission ends with the picture (or runs forever when --loop is set), or
 * on SIGTERM / SIGINT.
 *
 * @note Usage: pisstv --freq <Hz> (--image <path> | --stdin) [--mode <name>] [--loop] [-h | --help]
 *   - --freq      Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode
 *   - --image     Path to the picture (binary PPM, P6)
 *   - --stdin     Read the picture from stdin (pipe-friendly; --loop unsupported)
 *   - --mode      SSTV mode: martin-m1 (default) | martin-m2 | scottie-s1 | scottie-s2 |
 *                 robot-36 | robot-72 | pd-90 | pd-120 | pd-180
 *   - --loop      Retransmit the picture on EOF
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pisstv.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "pll_step_feedback.h"
#include "pnm_reader.h"
#include "realtime_scheduling.h"
#include "sstv_encoder.h"
#include "tone_sampler.h"

namespace pisstv {
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
         * @brief Read the header of the next picture of the input and check its size against mode.
         * @return Ok when a picture of the picture size of mode follows, End at the end of the
         *         input, Error after a diagnostic has been printed.
         */
        [[nodiscard]] PnmReadStatus readPictureHeader(PnmReader& reader, const SstvMode& mode) {
            const auto status{reader.readHeader()};
            if (status == PnmReadStatus::Error) {
                std::cerr << "[ERROR] pisstv: " << reader.lastError() << std::endl;
                return status;
            }

            const PnmHeader& header{reader.header()};
            if (status == PnmReadStatus::Ok && (header.width != mode.width || header.height != mode.height)) {
                std::cerr << "[ERROR] pisstv: the picture is " << header.width << "x" << header.height << ", --mode "
                          << mode.name << " transmits " << mode.width << "x" << mode.height << " pictures."
                          << std::endl;
                return PnmReadStatus::Error;
            }
            return status;
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], SstvParameters& params) {
        CLI::App app{"Slow-scan television transmitter (PPM picture -> librpitx ngfmdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq",
                       transmissionFrequencyText,
                       "Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        // --image and --stdin are mutually exclusive and exactly one is
        // required: an option group with require_option(1) lets CLI11 enforce
        // both invariants in one place and surfaces a clean diagnostic in
        // --help, instead of a manual post-parse check that would silently
        // accept neither or both.
        auto* inputGroup{app.add_option_group("input", "Picture input source (exactly one is required)")};
        inputGroup->add_option(
            "--image", params.imagePath, "Input picture path (binary PPM, P6, of the picture size of the mode)");
        inputGroup->add_flag(
            "--stdin", params.useStdin, "Read the picture from stdin (pipe-friendly; --loop unsupported)");
        inputGroup->require_option(1);
        app.add_flag("--loop", params.loop, "Retransmit the picture on EOF (requires --image)");

        // The --mode values and the picture size of every mode come from the mode table.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const SstvMode& mode: SSTV_MODES) {
            modeNames.emplace_back(mode.name);
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back() + " (" + std::to_string(mode.width) + "x" +
                        std::to_string(mode.height) + ")";
        }
        std::string modeName{params.mode->name};
        // IsMember replaces the value with the spelling of the table; its description is
        // overridden because the option description already lists the values.
        app.add_option("--mode", modeName, "SSTV mode and its picture size, " + modeName + " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames, CLI::ignore_case).description(""));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        // --loop rewinds the input at EOF, which stdin cannot do.
        if (params.useStdin && params.loop) {
            std::cerr << "[ERROR] --loop is unsupported with --stdin." << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.mode = &*std::ranges::find(SSTV_MODES, modeName, &SstvMode::name);
        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        SstvParameters params;
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
        const SstvMode& mode{*params.mode};

        // SIGTERM: stop cleanly when rpitx-ui or a service manager terminates us.
        std::signal(SIGTERM, handleSignal);
        // SIGINT: stop cleanly on Ctrl+C during manual runs.
        std::signal(SIGINT, handleSignal);
        // SIGPIPE: keep shutdown behavior explicit when launched from shell wrappers.
        std::signal(SIGPIPE, handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

        auto reader{makePnmReader(PnmFormat::Pixmap, params.useStdin, params.imagePath)};
        if (reader == nullptr) {
            return 1;
        }
        if (params.loop && reader->seekable() == false) {
            std::cerr << "[ERROR] Image source is not seekable; Loop is unsupported on this input." << std::endl;
            return 1;
        }

        // The first header is read before the DMA is set up, so a bad picture never reaches the hardware.
        auto status{readPictureHeader(*reader, mode)};
        if (status == PnmReadStatus::End) {
            std::cerr << "[ERROR] pisstv: no PPM image found in the input." << std::endl;
        }
        if (status != PnmReadStatus::Ok) {
            return 1;
        }

        std::cout << "pisstv: carrier=" << params.transmissionFrequency << " Hz, mode=" << mode.name
                  << ", format=" << reader->description()
                  << ", picture_time=" << std::lround(pictureTimeMs(mode) / 1000.0)
                  << " s, loop=" << (params.loop ? "yes" : "no") << std::endl;

        // Wrap DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll and PllStepFeedback refuse the PLL settings the board
        // cannot run and appendSequence / PnmReader throw on contract
        // violations. A stray throw escaping main would terminate via
        // std::terminate without flushing the diagnostic.
        try {
            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE};
            // The engine rounds every sample to the frequency step of the PLL; the rounding is carried into the next.
            PllStepFeedback<clkgpio> stepFeedback{dma, WHITE_HZ};
            // Slot 0 and the start-up burst of the engine get idle samples, so the first sample reaches the air.
            startTransmission(dma, IDLE_DEVIATION_HZ);

            ToneSampler sampler{SAMPLE_RATE};
            std::vector<Tone> tones;
            std::vector<float> samples;
            const std::size_t rowSize{static_cast<std::size_t>(mode.width) * reader->samplesPerPixel()};
            std::vector<std::uint8_t> rows(rowSize * static_cast<std::size_t>(rowsPerSequence(mode)));

            // Queue the tones in the DMA engine block by block, until a stop is requested.
            const auto transmit{[&dma, &sampler, &stepFeedback, &tones, &samples]() {
                samples.clear();
                sampler.append(tones, samples);
                stepFeedback.apply(samples);
                // SetFrequencySamples blocks until the block is in the DMA FIFO (see
                // ngfmdmasync.cpp upstream), so no manual pacing is needed here.
                for (std::size_t offset{0}; offset < samples.size() && running.load(std::memory_order_relaxed);
                     offset += DMA_BLOCK_SAMPLES) {
                    dma.SetFrequencySamples(samples.data() + offset,
                                            std::min(DMA_BLOCK_SAMPLES, samples.size() - offset));
                }
            }};

            while (status == PnmReadStatus::Ok && running.load(std::memory_order_relaxed)) {
                tones.clear();
                appendCalibrationHeader(mode, tones);
                transmit();

                while (reader->rowsRemaining() > 0 && running.load(std::memory_order_relaxed)) {
                    for (std::size_t offset{0}; offset < rows.size(); offset += rowSize) {
                        if (reader->readRow(std::span{rows}.subspan(offset, rowSize)) != PnmReadStatus::Ok) {
                            std::cerr << "[ERROR] pisstv: " << reader->lastError() << std::endl;
                            dma.stop();
                            return 1;
                        }
                    }
                    tones.clear();
                    appendSequence(mode, rows, tones);
                    transmit();
                }

                // The carrier is unmodulated between two pictures, also while the next one is awaited.
                tones.assign(1, Tone{IDLE_DEVIATION_HZ, PICTURE_GAP_MS});
                transmit();

                // Stop before the next header: reading it could block on stdin.
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                status = readPictureHeader(*reader, mode);
                if (status == PnmReadStatus::End && params.loop) {
                    if (reader->rewind() == false) {
                        std::cerr << "[ERROR] pisstv: " << reader->lastError() << std::endl;
                        dma.stop();
                        return 1;
                    }
                    status = readPictureHeader(*reader, mode);
                }
            }
            if (status == PnmReadStatus::Error) {
                dma.stop();
                return 1;
            }

            // The queued tones are transmitted when the input ended by itself, discarded on a stop request.
            const bool inputEnded{running.load(std::memory_order_relaxed) != 0};
            stopTransmission(dma, IDLE_DEVIATION_HZ, inputEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pisstv: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pisstv: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pisstv

int main(int argc, char* argv[]) {
    return pisstv::run(argc, argv);
}
