/**
 * @file pifsq.cpp
 * @brief FSQ (Fast Simple QSO) transmitter implementation.
 *
 * Transmits a text message in the FSQ mode of ZL2AFP and ZL1BPU: a sentence that
 * starts with the callsign of the sender, sent in a varicode as steps between 33
 * tones spaced 8.79 Hz apart, one tone per symbol of 512, 341, 256 or 171 ms (FSQ
 * 2, 3, 4.5 or 6). A directed message is a sentence of the FSQCall protocol for
 * the station of a callsign or for all stations, an undirected one a plain FSQ
 * sentence. FSQ is a sequence of audio tones sent over single sideband, so every
 * tone is transmitted as the carrier shifted above the requested frequency: a
 * receiver tuned to it in USB mode hears the tones at 1350 - 1631 Hz, where FSQ
 * decoders look for them. Transmission ends with the message, or on a stop signal.
 *
 * @note Usage: pifsq --freq <Hz> --callsign <callsign> --message "<text>" [--mode 2|3|4.5|6]
 *               [--type directed|undirected] [--to <callsign>] [-h | --help]
 *   - --freq      Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode
 *   - --callsign  Callsign of the sender: 1 - 19 letters, digits or /
 *   - --message   Message to transmit in printable ASCII characters (quote multi-word strings)
 *   - --mode      FSQ mode, the nominal baud rate: 2 (default) | 3 | 4.5 | 6
 *   - --type      Message type: directed (default) | undirected
 *   - --to        Callsign of the destination of a directed message: allcall (default) for all stations
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pifsq.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "pll_step_feedback.h"
#include "realtime_scheduling.h"
#include "stop_signals.h"

namespace pifsq {
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
         * @brief Length of a symbol of mode in milliseconds.
         */
        [[nodiscard]] constexpr double symbolMs(const FsqMode& mode) noexcept {
            return 1'000.0 * static_cast<double>(mode.symbolSamples) / SAMPLE_RATE;
        }
    }  // namespace

    void handleSignal([[maybe_unused]] int sig) {
        running.store(0, std::memory_order_relaxed);
    }

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], FsqParameters& params) {
        CLI::App app{"Fast Simple QSO (FSQ) transmitter (text message -> librpitx ngfmdmasync)"};

        std::string transmissionFrequencyText;
        app.add_option("--freq",
                       transmissionFrequencyText,
                       "Carrier frequency in Hz, the frequency a receiver is tuned to in USB mode")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        // Callsigns the encoder accepts, as --help and the diagnostics name them.
        const std::string callsignRule{"1 - " + std::to_string(MAX_CALLSIGN_LENGTH) + " letters, digits or /"};
        app.add_option("--callsign",
                       params.callsign,
                       "Callsign of the sender, sent as it is written (FSQ stations write theirs in lower case): " +
                           callsignRule)
            ->required();
        app.add_option("--message",
                       params.message,
                       "Message to transmit (quote multi-word strings, e.g. --message \"hello world\") in printable "
                       "ASCII characters: a lower-case letter, a space or a full stop takes one symbol, any other "
                       "character two")
            ->required();

        // The --mode values and the symbol length of every mode come from the mode table.
        std::vector<std::string> modeNames;
        std::string modeList;
        for (const FsqMode& mode: FSQ_MODES) {
            modeNames.emplace_back(mode.name);
            modeList += (modeList.empty() ? "" : " | ") + modeNames.back() + " (" +
                        std::to_string(std::lround(symbolMs(mode))) + " ms symbols)";
        }
        std::string modeName{params.mode->name};
        // The description of IsMember is overridden because the option description already lists the values.
        app.add_option("--mode", modeName, "FSQ mode, the nominal baud rate, " + modeName + " by default: " + modeList)
            ->transform(CLI::IsMember(modeNames).description(""));
        // IsMember replaces the value with the spelling of the table.
        const std::vector<std::string> typeNames{TYPE_NAMES.begin(), TYPE_NAMES.end()};
        std::string type{typeName(params.type)};
        app.add_option("--type",
                       type,
                       "Message type: directed (default, a sentence of the FSQCall protocol for the station of "
                       "--to) | undirected (an FSQ sentence for every station)")
            ->transform(CLI::IsMember(typeNames, CLI::ignore_case).description(""));
        const auto* destinationOption{
            app.add_option("--to",
                           params.destination,
                           "Callsign of the destination of a directed message, as that station writes it (" +
                               callsignRule + "): " + params.destination + " (default) for all stations")};

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (isCallsign(params.callsign) == false) {
            std::cerr << "[ERROR] --callsign must be " << callsignRule << "!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (params.message.empty()) {
            std::cerr << "[ERROR] --message must not be empty!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }
        if (isEncodable(params.message) == false) {
            std::cerr << "[ERROR] --message must contain only printable ASCII characters!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.type = static_cast<MessageType>(std::ranges::find(typeNames, type) - typeNames.begin());
        if (params.type == MessageType::Undirected) {
            // An undirected message has no destination.
            if (destinationOption->count() > 0) {
                std::cerr << "[ERROR] --to is not valid with --type undirected!" << std::endl;
                return rpitx::cli::ParseResult::Error;
            }
        } else if (isCallsign(params.destination) == false) {
            std::cerr << "[ERROR] --to must be " << callsignRule << "!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        params.mode = &*std::ranges::find(FSQ_MODES, modeName, &FsqMode::name);
        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    int run(int argc, char* argv[]) {
        FsqParameters params;
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
        const FsqMode& mode{*params.mode};
        const bool directed{params.type == MessageType::Directed};

        // Real-time scheduling: keep the DMA buffer fed while other processes load the CPU.
        rpitx::realtime::enableRealtimeScheduling();

        std::cout << "pifsq: message=\"" << params.message << "\"" << std::endl;

        // Wrap the encoding, DMA construction and the transmission loop in a try / catch:
        // checkCarrierPll refuses the PLL settings the board cannot run and
        // PllStepFeedback a carrier whose tones cross an integer step of the
        // PLL multiplier. A stray throw escaping main would terminate via
        // std::terminate without the destructor of ngfmdmasync stopping the
        // DMA engine and the clock generator.
        try {
            const auto tones{encodeTransmission(params.type, params.callsign, params.destination, params.message)};

            std::cout << "pifsq: carrier=" << params.transmissionFrequency << " Hz, mode=" << mode.name
                      << ", symbol=" << symbolMs(mode) << " ms, type=" << typeName(params.type)
                      << ", callsign=" << params.callsign << (directed ? ", to=" + params.destination : "")
                      << ", transmission_time="
                      << std::lround(static_cast<double>(tones.size()) * symbolMs(mode) / 1'000.0) << " s" << std::endl;

            // Stop signals: end the transmission cleanly, also when the terminal closes.
            rpitx::realtime::installStopSignalHandlers(handleSignal);
            // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
            rpitx::clock::checkCarrierPll(params.transmissionFrequency, SAMPLE_RATE);
            ngfmdmasync dma{params.transmissionFrequency, SAMPLE_RATE, DMA_CHANNEL, DMA_FIFO_SIZE};
            // The engine rounds every sample to the frequency step of the PLL; the rounding is carried into the next.
            rpitx::clock::PllStepFeedback<clkgpio> stepFeedback{dma, HIGHEST_TONE_HZ};
            // Slot 0 and the start-up burst of the engine get idle samples, so the first sample reaches the air.
            rpitx::dma::startTransmission(dma, IDLE_DEVIATION_HZ);

            // Queue the tones in the DMA engine block by block, until a stop is requested.
            std::vector<float> block(DMA_BLOCK_SAMPLES);
            for (const uint8_t tone: tones) {
                if (running.load(std::memory_order_relaxed) == 0) {
                    break;
                }
                const float toneFrequency{BASE_TONE_HZ + static_cast<float>(tone) * TONE_SPACING_HZ};
                for (std::size_t queued{0}; queued < mode.symbolSamples && running.load(std::memory_order_relaxed);
                     queued += block.size()) {
                    std::ranges::fill(block, toneFrequency);
                    stepFeedback.apply(block);
                    // SetFrequencySamples blocks until the block is in the DMA FIFO (see
                    // ngfmdmasync.cpp upstream), so no manual pacing is needed here.
                    dma.SetFrequencySamples(block.data(), block.size());
                }
            }

            // The queued tones are transmitted when the message ended, discarded on a stop request.
            const bool messageEnded{running.load(std::memory_order_relaxed) != 0};
            rpitx::dma::stopTransmission(dma, IDLE_DEVIATION_HZ, messageEnded);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pifsq: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pifsq: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pifsq

int main(int argc, char* argv[]) {
    return pifsq::run(argc, argv);
}
