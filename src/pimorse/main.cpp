/**
 * @file main.cpp
 * @brief Morse code CW OOK transmitter.
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

#include <librpitx/librpitx.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli_utils.h"
#include "morse_encoder.h"

/**
 * @brief OOK upsample factor (symbol duration = upsample / symbolrate seconds).
 */
static constexpr float OOK_UPSAMPLE{125.0f};

/**
 * @brief DMA bit depth for ookburst.
 */
static constexpr int OOK_DMA_BITS{14};

/**
 * @brief Divisor to convert words-per-minute to OOK symbol rate.
 *
 * PARIS standard: the word "PARIS" contains 50 dit-units, so 1 dit = 1200/WPM ms
 * and symbolRate = WPM / 1.2.
 */
static constexpr float WPM_TO_SYMBOL_RATE_DIVISOR{1.2f};

/**
 * @brief Transmit a CW OOK binary string at the given frequency and symbol rate.
 * @param freq RF center frequency in Hz.
 * @param symbolRate Symbol rate in Hz.
 * @param cw Binary CW string ('0' = off, '1' = on).
 */
static void sendCwOok(float freq, float symbolRate, std::string_view cw) {
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

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: pimorse <freq_Hz> <WPM> <\"message\">" << std::endl;
        return 1;
    }

    const auto freqOpt{parseNumericArg<float>(argv[1])};
    const auto wpmOpt{parseNumericArg<float>(argv[2])};
    if (freqOpt == std::nullopt || wpmOpt == std::nullopt) {
        std::cerr << "[ERROR] Invalid numeric argument!" << std::endl;
        return 1;
    }
    const float freq{freqOpt.value()};
    const float wpm{wpmOpt.value()};
    if (freq <= 0.0f || wpm <= 0.0f) {
        std::cerr << "[ERROR] Frequency and WPM must be positive values!" << std::endl;
        return 1;
    }

    const std::string_view msg{argv[3]};
    const auto symbolRate{wpm / WPM_TO_SYMBOL_RATE_DIVISOR};

    std::cout << "Message: " << msg << std::endl;

    for (size_t i{0}; i < msg.size(); ++i) {
        const auto morse{charToMorse(msg[i])};
        if (morse == std::nullopt) {
            std::cout << "Message[" << std::setw(2) << std::setfill('0') << i << "]: " << msg[i]
                      << "\tskipped (unsupported character)" << std::endl;
            continue;
        }

        const auto cw{morseToCw(*morse)};
        std::cout << "Message[" << std::setw(2) << std::setfill('0') << i
                  << "]: " << static_cast<char>(std::toupper(static_cast<unsigned char>(msg[i]))) << "\tmorse["
                  << *morse << "]\tcw[" << cw << "]" << std::endl;
        sendCwOok(freq, symbolRate, cw);
    }

    return 0;
}
