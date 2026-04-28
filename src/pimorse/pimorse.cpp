/**
 * @file pimorse.cpp
 * @brief Morse code CW OOK transmitter implementation.
 *
 * Converts a text message to Morse code and transmits it as on-off keying (OOK)
 * at the specified RF frequency and words-per-minute rate.
 *
 * @note Usage: pimorse --freq <Hz> --wpm <value> --message "<text>" [-h | --help]
 *   - --freq      Carrier frequency in Hz
 *   - --wpm       Speed in words per minute (positive finite)
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

#include <CLI/CLI.hpp>
#include <librpitx/librpitx.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "cli_common.h"
#include "cli_validators.h"
#include "morse_encoder.h"

namespace pimorse {
    rpitx::cli::ParseResult parseArgs(int argc, char* argv[], PimorseParameters& params) {
        CLI::App app{"Morse code CW OOK transmitter"};

        std::string freqText;
        app.add_option("--freq", freqText, "Carrier frequency in Hz")
            ->required()
            ->check(rpitx::cli::validators::FrequencyHz);
        app.add_option("--wpm", params.wpm, "Speed in words per minute (positive finite)")
            ->required()
            ->check(rpitx::cli::validators::PositiveFiniteFloat);
        app.add_option("--message", params.message,
                       "Message to encode and transmit (quote multi-word strings, e.g. --message \"CQ CQ DE RPITX\")")
            ->required();

        if (const auto result{rpitx::cli::finalizeParse(app, argc, argv)};
            result != rpitx::cli::ParseResult::Ok) {
            return result;
        }

        if (params.message.empty()) {
            std::cerr << "[ERROR] --message must not be empty!" << std::endl;
            return rpitx::cli::ParseResult::Error;
        }

        return rpitx::cli::assignFrequencyHz(freqText, params.freq);
    }

    std::string encodeMessage(std::string_view message) {
        std::string encodedMessage;
        for (std::size_t i{0}; i < message.size(); ++i) {
            const auto morse{charToMorse(message[i])};
            if (morse == std::nullopt) {
                std::cout << "Message[" << std::setw(2) << std::setfill('0') << i << "]: " << message[i]
                          << "\tskipped (unsupported character)" << std::endl;
                continue;
            }

            const auto cw{morseToCw(morse.value())};
            std::cout << "Message[" << std::setw(2) << std::setfill('0') << i
                      << "]: " << static_cast<char>(std::toupper(static_cast<unsigned char>(message[i]))) << "\tmorse["
                      << morse.value() << "]\tcw[" << cw << "]" << std::endl;
            encodedMessage += cw;
        }

        return encodedMessage;
    }

    void sendCwOok(float freq, float symbolRate, std::string_view cw) {
        const auto fifoSize{static_cast<int>(cw.size()) - 1};
        if (fifoSize <= 0) {
            return;
        }

        ookburst ook(freq, symbolRate, OOK_DMA_BITS, fifoSize, OOK_UPSAMPLE);

        std::vector<unsigned char> symbols(fifoSize);
        std::ranges::transform(cw.substr(0, fifoSize), symbols.begin(), [](char c) -> unsigned char {
            if (c != '0') {
                return 1;
            }
            return 0;
        });
        ook.SetSymbols(symbols.data(), fifoSize);
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

        std::cout << "Message: " << params.message << std::endl;

        const auto encodedMessage{encodeMessage(params.message)};
        const auto symbolRate{params.wpm / WPM_TO_SYMBOL_RATE_DIVISOR};

        // ookburst takes the carrier frequency as float; --freq is parsed and
        // validated as integer Hz per the CLI v2 contract, then narrowed at the
        // call boundary.
        sendCwOok(static_cast<float>(params.freq), symbolRate, encodedMessage);

        return 0;
    }
}  // namespace pimorse

int main(int argc, char* argv[]) {
    return pimorse::run(argc, argv);
}
