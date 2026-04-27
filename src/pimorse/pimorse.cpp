/**
 * @file pimorse.cpp
 * @brief Morse code CW OOK transmitter implementation.
 *
 * Converts a text message to Morse code and transmits it as on-off keying (OOK)
 * at the specified RF frequency and words-per-minute rate.
 *
 * @note Usage: pimorse <freq_Hz> <WPM> <"message">
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 04.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pimorse.h"

#include <librpitx/librpitx.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <vector>

#include "morse_encoder.h"

namespace pimorse {
    void printUsage() {
        std::cerr << "Usage: pimorse <freq_Hz> <WPM> <\"message\">" << std::endl;
    }

    ParseResult parseArgs(int argc, char* argv[], PimorseParameters& params) {
        if (argc < 4) {
            printUsage();
            return ParseResult::Error;
        }

        const auto freqOpt{parseNumericArg<float>(argv[1])};
        const auto wpmOpt{parseNumericArg<float>(argv[2])};
        if (freqOpt == std::nullopt || wpmOpt == std::nullopt) {
            std::cerr << "[ERROR] Invalid numeric argument!" << std::endl;
            return ParseResult::Error;
        }
        const float freq{freqOpt.value()};
        const float wpm{wpmOpt.value()};
        if (freq <= 0.0F || wpm <= 0.0F) {
            std::cerr << "[ERROR] Frequency and WPM must be positive values!" << std::endl;
            return ParseResult::Error;
        }

        params.freq    = freq;
        params.wpm     = wpm;
        params.message = argv[3];
        return ParseResult::Ok;
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

            const auto cw{morseToCw(*morse)};
            std::cout << "Message[" << std::setw(2) << std::setfill('0') << i
                      << "]: " << static_cast<char>(std::toupper(static_cast<unsigned char>(message[i]))) << "\tmorse["
                      << *morse << "]\tcw[" << cw << "]" << std::endl;
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
        if (parseArgs(argc, argv, params) != ParseResult::Ok) {
            return 1;
        }

        std::cout << "Message: " << params.message << std::endl;

        const auto encodedMessage{encodeMessage(params.message)};
        const auto symbolRate{params.wpm / WPM_TO_SYMBOL_RATE_DIVISOR};

        sendCwOok(params.freq, symbolRate, encodedMessage);

        return 0;
    }
}  // namespace pimorse

int main(int argc, char* argv[]) {
    return pimorse::run(argc, argv);
}
