/**
 * @file pimorse.cpp
 * @brief Morse code CW OOK transmitter implementation.
 *
 * Converts a text message to Morse code and transmits it as on-off keying (OOK)
 * at the specified RF frequency and words-per-minute rate. Transmission ends with
 * the message, or after the character in progress on a stop signal.
 *
 * @note Usage: pimorse --freq <Hz> --wpm <value> --message "<text>" [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --wpm       Speed in words per minute (1 - 200)
 *   - --message   Message to encode and transmit (quote multi-word strings)
 *   - -h, --help  Print this help message and exit
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pimorse.h"

#include <librpitx/librpitx.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "carrier_pll.h"
#include "cli_common.h"
#include "cli_validators.h"
#include "dma_transmission.h"
#include "morse_encoder.h"
#include "realtime_scheduling.h"
#include "stop_signals.h"

namespace pimorse {
    namespace {
        /**
         * @brief Samples by which every DMA chain of ookburst after the first starts late.
         *
         * The pacing FIFO still holds PCM_FIFO_DEPTH samples of the previous chain, and the
         * fill block of the new one adds one.
         */
        constexpr std::size_t CHAIN_START_SAMPLES{PCM_FIFO_DEPTH + 1};
        static_assert(CHAIN_START_SAMPLES <= OOK_UPSAMPLE,
                      "the trimmed samples must lie in the silent last symbol of a character");

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

    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PimorseParameters& params) {
        CLI::App app{"Morse code CW OOK transmitter"};

        std::string transmissionFrequencyText;
        app.add_option("--freq", transmissionFrequencyText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--wpm", params.wpm, "Speed in words per minute")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat)
            ->check(CLI::Range(MIN_WPM, MAX_WPM));
        app.add_option("--message",
                       params.message,
                       "Message to encode and transmit (quote multi-word strings, e.g. --message \"CQ CQ DE RPITX\")")
            ->required();

        if (const auto result{rpitx::cli::parseCliApp(app, argc, argv)}; result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (params.message.empty()) {
            std::cerr << "[ERROR] --message must not be empty!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        return rpitx::cli::assignFrequencyHz(transmissionFrequencyText, params.transmissionFrequency);
    }

    std::vector<std::string> encodeMessage(std::string_view message) {
        std::vector<std::string> characters;
        for (std::size_t i{0}; i < message.size(); ++i) {
            const auto morse{charToMorse(message[i])};
            if (morse == std::nullopt) {
                std::cout << "pimorse: message[" << std::setw(2) << std::setfill('0') << i << "]=\"" << message[i]
                          << "\", skipped (unsupported character)" << std::endl;
                continue;
            }

            auto cw{morseToCw(morse.value())};
            std::cout << "pimorse: message[" << std::setw(2) << std::setfill('0') << i << "]=\""
                      << static_cast<char>(std::toupper(static_cast<unsigned char>(message[i]))) << "\", morse=\""
                      << morse.value() << "\", cw=" << cw << std::endl;
            characters.push_back(std::move(cw));
        }

        return characters;
    }

    void sendCwOok(uint64_t transmissionFrequency, float symbolRate, const std::vector<std::string>& characters) {
        std::size_t longestCharacterSymbols{0};
        for (const auto& cw: characters) {
            longestCharacterSymbols = std::max(longestCharacterSymbols, cw.size());
        }
        if (longestCharacterSymbols == 0) {
            return;
        }

        // Refuse the carriers the board cannot run before the engine programs the PLL unguarded.
        checkCarrierPll(transmissionFrequency, 0);
        // ookburst keys every value for one sample (upsample 1); the samples of every character are rendered below.
        const std::size_t maxSamples{longestCharacterSymbols * OOK_UPSAMPLE};
        // librpitx truncates the pacing rate to whole Hz, so it is rounded to the nearest whole Hz first.
        ookburst ook{transmissionFrequency,
                     std::round(symbolRate * static_cast<float>(OOK_UPSAMPLE)),
                     DMA_CHANNEL,
                     static_cast<uint32_t>(maxSamples)};

        // One DMA chain per character: SetSymbols returns once it is sent, so a stop request ends the message there.
        std::vector<unsigned char> samples;
        samples.reserve(maxSamples);
        for (std::size_t index{0}; index < characters.size(); ++index) {
            if (running.load(std::memory_order_relaxed) == 0) {
                break;
            }
            samples.clear();
            for (const char c: characters[index]) {
                samples.insert(samples.end(), OOK_UPSAMPLE, static_cast<unsigned char>(c != '0'));
            }
            // The chain of the next character starts late, so the gap behind this one is shortened by as much.
            if (index + 1 < characters.size()) {
                samples.resize(samples.size() - CHAIN_START_SAMPLES);
            }
            ook.SetSymbols(samples.data(), static_cast<uint32_t>(samples.size()));
        }
    }

    int run(int argc, char* argv[]) {
        PimorseParameters params;
        switch (parseArgs(argc, argv, params)) {
            case rpitx::cli::ParseResult::Ok:
                break;
            case rpitx::cli::ParseResult::Help:
                return 0;
            case rpitx::cli::ParseResult::Error:
                return 1;
        }

        // Real-time scheduling: start the DMA chain of every character in time while other processes load the CPU.
        enableRealtimeScheduling();

        std::cout << "pimorse: message=\"" << params.message << "\"" << std::endl;

        const auto characters{encodeMessage(params.message)};
        const auto symbolRate{params.wpm / WPM_TO_SYMBOL_RATE_DIVISOR};
        // sendCwOok keys every symbol of every character, each for 1 / symbolRate seconds.
        std::size_t symbolCount{0};
        for (const auto& cw: characters) {
            symbolCount += cw.size();
        }
        const double transmissionSeconds{static_cast<double>(symbolCount) / symbolRate};

        // Wrap the OOK setup in a try / catch: checkCarrierPll refuses the PLL
        // settings the board cannot run with an exception. A stray throw escaping
        // main would terminate via std::terminate without the destructor of
        // ookburst stopping the clock generator.
        try {
            std::cout << "pimorse: carrier=" << params.transmissionFrequency << " Hz, wpm=" << params.wpm
                      << ", transmission_time=" << std::lround(transmissionSeconds) << " s" << std::endl;

            // Stop signals: end the transmission cleanly, also when the terminal closes.
            installStopSignalHandlers(handleSignal);
            sendCwOok(params.transmissionFrequency, symbolRate, characters);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] pimorse: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "pimorse: transmission stopped." << std::endl;
        return 0;
    }
}  // namespace pimorse

int main(int argc, char* argv[]) {
    return pimorse::run(argc, argv);
}
