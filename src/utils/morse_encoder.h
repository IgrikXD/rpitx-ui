/**
 * @file morse_encoder.h
 * @brief Morse code lookup and CW OOK binary conversion.
 *
 * Provides a constexpr ITU Morse code table (A-Z, 0-9, space) and functions
 * to convert text characters into on-off keying (OOK) binary strings suitable
 * for RF transmission.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 04.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

/**
 * @brief Morse code entry mapping an ASCII character to its dit/dah pattern.
 */
struct MorseEntry {
    char ch;                ///< ASCII character (uppercase letter, digit, or space).
    std::string_view dits;  ///< Morse pattern: '.' = dit, '-' = dah, ' ' = inter-element gap.
};

/**
 * @brief ITU Morse code table (A-Z, 0-9, space).
 *
 * Each entry's dits field uses trailing spaces as inter-character gap (2 units).
 * Space character uses 4 dits of silence for inter-word gap.
 */
// clang-format off
inline constexpr std::array<MorseEntry, 37> MORSE_TABLE{{
    {' ', "    "},
    {'0', "-----  "},
    {'1', ".----  "},
    {'2', "..---  "},
    {'3', "...--  "},
    {'4', "....-  "},
    {'5', ".....  "},
    {'6', "-....  "},
    {'7', "--...  "},
    {'8', "---..  "},
    {'9', "----.  "},
    {'A', ".-  "},
    {'B', "-...  "},
    {'C', "-.-.  "},
    {'D', "-..  "},
    {'E', ".  "},
    {'F', "..-.  "},
    {'G', "--.  "},
    {'H', "....  "},
    {'I', "..  "},
    {'J', ".---  "},
    {'K', "-.-  "},
    {'L', ".-..  "},
    {'M', "--  "},
    {'N', "-.  "},
    {'O', "---  "},
    {'P', ".--.  "},
    {'Q', "--.-  "},
    {'R', ".-.  "},
    {'S', "...  "},
    {'T', "-  "},
    {'U', "..-  "},
    {'V', "...-  "},
    {'W', ".--  "},
    {'X', "-..-  "},
    {'Y', "-.--  "},
    {'Z', "--..  "},
}};
// clang-format on

static_assert(std::ranges::is_sorted(MORSE_TABLE, {}, &MorseEntry::ch),
              "MORSE_TABLE must be sorted by 'ch' in ascending ASCII order for lower_bound lookup");

/**
 * @brief Look up the Morse pattern for a given ASCII character.
 * @param ch Input character (case-insensitive for letters).
 * @return Morse dit/dah pattern, or std::nullopt if the character is not in the ITU table.
 */
[[nodiscard]] inline std::optional<std::string_view> charToMorse(char ch) {
    const auto upper{static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))};
    const auto it{std::ranges::lower_bound(MORSE_TABLE, upper, {}, &MorseEntry::ch)};
    if (it != MORSE_TABLE.end() && it->ch == upper) {
        return it->dits;
    }
    return std::nullopt;
}

/**
 * @brief Convert a Morse dit/dah pattern to a binary CW OOK string.
 *
 * Encoding: dit -> "10", dah -> "1110", space -> "0".
 * The resulting string contains only '0' and '1' characters.
 *
 * @param morse Morse pattern (e.g., ".-  " for 'A').
 * @return Binary CW string (e.g., "10111000" for 'A').
 */
[[nodiscard]] std::string morseToCw(std::string_view morse);
