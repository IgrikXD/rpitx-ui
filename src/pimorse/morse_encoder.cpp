/**
 * @file morse_encoder.cpp
 * @brief Morse code CW OOK binary conversion implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 04.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "morse_encoder.h"

std::string morseToCw(std::string_view morse) {
    std::string cw;
    cw.reserve(morse.size() * 4);
    for (const char c: morse) {
        switch (c) {
            case '.':
                cw += "10";
                break;
            case '-':
                cw += "1110";
                break;
            case ' ':
                cw += '0';
                break;
            default:
                break;
        }
    }
    return cw;
}
