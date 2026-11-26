/**
 * @file piopera.cpp
 * @brief Opera beacon transmitter implementation.
 *
 * Transmits a callsign in the Opera beacon mode of EA5HVK: 239 symbols of on-off
 * keying of the carrier, 128 ms each in the Op05 mode and twice as long in every
 * following mode up to Op32. Opera decoders receive the carrier in USB mode as a
 * 1500 Hz tone, with the receiver tuned 1500 Hz below the carrier. Transmission
 * ends with the last symbol, or on a stop signal.
 *
 * @note Usage: piopera --freq <Hz> --callsign <callsign> [--mode <name>] [-h | --help]
 *   - --freq      Carrier frequency in Hz, 1500 Hz above the dial frequency of a USB receiver
 *   - --callsign  Callsign to transmit: one or two letters or digits, a digit and one to three letters
 *   - --mode      Opera mode: op05 (default) | op1 | op2 | op4 | op8 | op16 | op32
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 18.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "piopera.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "opera_encoder.h"
#include "realtime_scheduling.h"
#include "stop_signals.h"

namespace piopera {
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
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], OperaParameters& params) {
        CLI::App app{"Opera beacon transmitter (callsign -> librpitx iqdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq",
                       transmissionFrequencyText,
                       "Carrier frequency in Hz, 1500 Hz above the dial frequency of a USB receiver")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--callsign",
                       params.callsign,
                       "Callsign to transmit: one or two letters or digits, a digit and one to three letters (e.g. "
                       "Q1A, Q0CAL, QA1ABC); letters are sent in upper case")
            ->required();

        // The --mode values come from the mode table.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const OperaMode& mode: OPERA_MODES) {
            modeNames.emplace_back(mode.name);
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back();
        }
        std::string modeName{params.mode->name};
        // IsMember replaces the value with the spelling of the table; its description is
        // overridden because the option description already lists the values.
        app.add_option("--mode",
                       modeName,
                       "Opera mode, named after the transmission time in minutes (op05 stands for half a minute), " +
                           modeName + " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames, CLI::ignore_case).description(""));

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (isEncodable(params.callsign) == false) {
            std::cerr << "[ERROR] --callsign must be one or two letters or digits, a digit and one to three letters!"
                      << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        std::ranges::transform(params.callsign, params.callsign.begin(), [](char ch) {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        });

        params.mode = &*std::ranges::find(OPERA_MODES, modeName, &OperaMode::name);
        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        OperaParameters params;
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
        const OperaMode& mode{*params.mode};

        // Stop signals: end the transmission cleanly, also when the terminal closes.
        installStopSignalHandlers(handleSignal);

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        enableRealtimeScheduling();

        // Wrap the encoding, DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll refuses the PLL settings the board cannot run with an exception.
        // A stray throw escaping main would terminate via std::terminate without the
        // destructor of iqdmasync stopping the DMA engine and the clock generator.
        try {
            const auto symbols{encodeTransmission(params.callsign)};

            std::cout << "piopera: carrier=" << params.transmissionFrequency << " Hz, mode=" << mode.name
                      << ", symbol=" << mode.symbolMs << " ms, callsign=" << params.callsign << ", transmission_time="
                      << std::lround(static_cast<double>(SYMBOL_COUNT * mode.symbolMs) / 1'000.0) << " s" << std::endl;

            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            iqdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE, MODE_FREQ_A};
            // Slot 0 and the start-up burst of the engine get carrier-off samples, so the first symbol reaches the air.
            startTransmission(dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC);

            // Queue the symbols in the DMA engine block by block, until a stop is requested.
            const std::size_t symbolSamples{samplesPerSymbol(mode)};
            std::vector<std::complex<float>> block(DMA_BLOCK_SAMPLES);
            for (const bool carrierOn: symbols) {
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                std::ranges::fill(block, carrierOn ? CARRIER_ON_SAMPLE : CARRIER_OFF_SAMPLE);
                for (std::size_t queued{0}; queued < symbolSamples && running.load(std::memory_order_relaxed);
                     queued += block.size()) {
                    // SetIQSamples blocks until the block is in the DMA FIFO (see
                    // iqdmasync.cpp upstream), so no manual pacing is needed here.
                    dma.SetIQSamples(block.data(), block.size(), DEFAULT_HARMONIC);
                }
            }

            // The queued symbols are transmitted when the transmission ended by itself, discarded on a stop request.
            const bool transmissionEnded{running.load(std::memory_order_relaxed) != 0};
            stopTransmission(dma, CARRIER_OFF_SAMPLE, DEFAULT_HARMONIC, transmissionEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] piopera: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "piopera: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace piopera

int main(int argc, char* argv[]) {
    return piopera::run(argc, argv);
}
